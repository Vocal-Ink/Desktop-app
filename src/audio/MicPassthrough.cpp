#include "audio/MicPassthrough.h"

#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "audio/MicCapture.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kLevelIntervalMs = 50; // ~20 Hz
constexpr int kCaptureBufferMs = 20;
constexpr float kGateOffDb = -90.0f;
constexpr float kGateAttackS = 0.010f;
constexpr float kGateReleaseS = 0.150f;
constexpr float kGateHoldS = 0.060f;
constexpr float kEnvelopeDecayS = 0.020f;
constexpr float kHysteresisDb = 3.0f;
constexpr float kGainGlideS = 0.005f;

// Perceptual-ish meter: -60..0 dBFS -> 0..1.
float meterLevel(float rms)
{
    const float db = 20.0f * std::log10(rms + 1e-9f);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}
} // namespace

// --- Processor -------------------------------------------------------------------------

void MicPassthrough::Processor::setSampleRate(int rate)
{
    m_rate = rate > 0 ? rate : SampleRate;
    updateCoefficients();
}

void MicPassthrough::Processor::updateCoefficients()
{
    const float rate = float(m_rate);
    m_envDecay = std::exp(-1.0f / (kEnvelopeDecayS * rate));
    m_attackStep = 1.0f / (kGateAttackS * rate);
    m_releaseStep = 1.0f / (kGateReleaseS * rate);
    m_holdFrames = int(kGateHoldS * rate);
    m_gainGlide = 1.0f - std::exp(-1.0f / (kGainGlideS * rate));
}

void MicPassthrough::Processor::setGainDb(float db)
{
    m_gainTarget = AudioConvert::dbToGain(std::clamp(db, -24.0f, 24.0f));
}

void MicPassthrough::Processor::setGateDb(float thresholdDb)
{
    const bool enabled = thresholdDb > kGateOffDb;
    m_openLevel = AudioConvert::dbToGain(thresholdDb);
    m_closeLevel = AudioConvert::dbToGain(thresholdDb - kHysteresisDb);
    if (enabled != m_gateEnabled) {
        m_gateEnabled = enabled;
        reset();
    }
}

void MicPassthrough::Processor::reset()
{
    if (m_attackStep <= 0.0f)
        updateCoefficients();
    m_env = 0.0f;
    m_hold = 0;
    m_open = false;
    m_gain = m_gainTarget;
    m_gate = m_gateEnabled ? 0.0f : 1.0f;
}

void MicPassthrough::Processor::process(float *samples, qsizetype count)
{
    if (m_attackStep <= 0.0f)
        updateCoefficients();
    for (qsizetype i = 0; i < count; ++i) {
        const float x = samples[i];
        if (m_gateEnabled) {
            const float a = std::fabs(x);
            m_env = a > m_env ? a : m_env * m_envDecay;
            if (m_env >= m_openLevel) {
                m_open = true;
                m_hold = m_holdFrames;
            } else if (m_open) {
                if (m_env >= m_closeLevel)
                    m_hold = m_holdFrames;
                else if (m_hold > 0)
                    --m_hold;
                else
                    m_open = false;
            }
            if (m_open)
                m_gate = std::min(1.0f, m_gate + m_attackStep);
            else
                m_gate = std::max(0.0f, m_gate - m_releaseStep);
        }
        if (m_gain != m_gainTarget) {
            m_gain += (m_gainTarget - m_gain) * m_gainGlide;
            if (std::fabs(m_gain - m_gainTarget) < 1e-5f)
                m_gain = m_gainTarget;
        }
        samples[i] = x * m_gate * m_gain;
    }
}

// --- MicPassthrough ----------------------------------------------------------------

class MicPassthrough::Private
{
public:
    MicCapture *capture = nullptr;
    QByteArray deviceId;
    Processor proc;
    bool useDevice = true;
    bool starting = false;
    bool startFailed = false;
    QElapsedTimer levelTimer;
    float levelMax = 0.0f;
};

MicPassthrough::MicPassthrough(AudioPlayer *player, QObject *parent)
    : QObject(parent)
    , d(new Private)
    , m_player(player)
{
    d->proc.setSampleRate(SampleRate);
    d->proc.setGateDb(kGateOffDb);
}

MicPassthrough::~MicPassthrough()
{
    delete d->capture;
    delete d;
}

void MicPassthrough::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    setLive(mode == Mode::AlwaysOn);
}

void MicPassthrough::setInputDevice(const QByteArray &deviceId)
{
    d->deviceId = deviceId;
    if (d->capture)
        d->capture->setDevice(deviceId); // restarts it if running
}

void MicPassthrough::setGainDb(float db)
{
    d->proc.setGainDb(db);
}

void MicPassthrough::setGateDb(float thresholdDb)
{
    d->proc.setGateDb(thresholdDb);
}

void MicPassthrough::setDuckDuringSpeech(bool enabled, float duckDb)
{
    if (m_player)
        m_player->setLiveDuckingDb(enabled ? duckDb : 0.0f);
}

void MicPassthrough::press()
{
    if (m_mode == Mode::HoldKey)
        setLive(true);
}

void MicPassthrough::release()
{
    if (m_mode == Mode::HoldKey)
        setLive(false);
}

void MicPassthrough::toggle()
{
    if (m_mode == Mode::ToggleKey || m_mode == Mode::AlwaysOn)
        setLive(!m_live);
}

void MicPassthrough::setLive(bool live)
{
    if (m_mode == Mode::Off)
        live = false;
    if (m_live == live)
        return;
    if (live && !startCapture())
        return; // errorOccurred() explains why
    if (!live)
        stopCapture();
    m_live = live;
    emit liveChanged(live);
    if (!live)
        emit levelChanged(0.0f);
}

void MicPassthrough::setDeviceCaptureEnabled(bool enabled)
{
    d->useDevice = enabled;
}

bool MicPassthrough::startCapture()
{
    d->proc.reset();
    d->levelTimer.invalidate();
    d->levelMax = 0.0f;
    if (!d->useDevice)
        return true;
    if (!d->capture) {
        d->capture = new MicCapture(this);
        d->capture->setOutputRate(SampleRate);
        d->capture->setBufferDuration(kCaptureBufferMs);
        connect(d->capture, &MicCapture::samples, this, [this](const QVector<float> &mono) {
            processInput(mono, d->capture->outputRate());
        });
        connect(d->capture, &MicCapture::errorOccurred, this, &MicPassthrough::onCaptureError);
    }
    d->capture->setDevice(d->deviceId);
    d->startFailed = false;
    d->starting = true;
    d->capture->start(); // false without an error while a permission prompt is open
    d->starting = false;
    return !d->startFailed;
}

void MicPassthrough::stopCapture()
{
    if (d->capture)
        d->capture->stop(); // releases the device
}

void MicPassthrough::onCaptureError(const QString &message)
{
    if (d->starting) {
        d->startFailed = true;
    } else if (m_live) {
        stopCapture();
        m_live = false;
        emit liveChanged(false);
        emit levelChanged(0.0f);
    }
    emit errorOccurred(message);
}

void MicPassthrough::processInput(const QVector<float> &mono, int sampleRate)
{
    if (!m_live || mono.isEmpty() || sampleRate <= 0)
        return;
    if (d->proc.sampleRate() != sampleRate)
        d->proc.setSampleRate(sampleRate);

    d->levelMax = std::max(d->levelMax, meterLevel(AudioConvert::rms(mono.constData(), mono.size())));
    if (!d->levelTimer.isValid() || d->levelTimer.elapsed() >= kLevelIntervalMs) {
        emit levelChanged(d->levelMax);
        d->levelMax = 0.0f;
        d->levelTimer.start();
    }

    QVector<float> out = mono;
    d->proc.process(out.data(), out.size());
    if (m_player)
        m_player->writeLive(out, sampleRate);
}

MicPassthrough::Mode MicPassthrough::modeFromString(const QString &s)
{
    if (s == QLatin1String("hold"))
        return Mode::HoldKey;
    if (s == QLatin1String("toggle"))
        return Mode::ToggleKey;
    if (s == QLatin1String("always"))
        return Mode::AlwaysOn;
    return Mode::Off;
}

QString MicPassthrough::modeToString(Mode m)
{
    switch (m) {
    case Mode::HoldKey: return QStringLiteral("hold");
    case Mode::ToggleKey: return QStringLiteral("toggle");
    case Mode::AlwaysOn: return QStringLiteral("always");
    case Mode::Off: break;
    }
    return QStringLiteral("off");
}
