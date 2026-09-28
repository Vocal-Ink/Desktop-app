#include "audio/RoutingCheck.h"

#include "audio/AudioConvert.h"
#include "audio/AudioOutputLane.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <cmath>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QCoreApplication>
#include <QPermissions>
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSignalRate = 48000;
constexpr int kToneMs = 400;
constexpr int kLeadInMs = 150; // let the capture settle before the tone starts
constexpr int kGraceMs = 700;  // after the tone: device buffers and cable latency
constexpr int kBlockMs = 50;   // 20 Hz bins: 880 and 1320 Hz sit exactly on them
constexpr float kToneShare = 0.5f;
constexpr float kMinRms = 3e-4f; // about -70 dBFS
constexpr int kHitsNeeded = 2;   // per tone
constexpr float kSomethingLevel = 0.05f; // meter level that counts as "something arrived"

constexpr quint64 kToneSoundId = 1;

float meterLevel(float rms)
{
    const float db = 20.0f * std::log10(rms + 1e-9f);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

double goertzelPower(const QVector<float> &x, double freq, int rate)
{
    const double coeff = 2.0 * std::cos(2.0 * kPi * freq / rate);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        const double s0 = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

} // namespace

// --- Detector --------------------------------------------------------------------------

RoutingCheck::Detector::Detector(int sampleRate)
    : m_rate(sampleRate > 0 ? sampleRate : kSignalRate)
    , m_blockLen(std::max<qsizetype>(64, qsizetype(m_rate) * kBlockMs / 1000))
{
    m_block.reserve(m_blockLen);
}

float RoutingCheck::Detector::toneShare(const float *block, qsizetype count, double freq, int sampleRate)
{
    if (count < 8 || sampleRate <= 0)
        return 0.0f;
    QVector<float> w(count);
    double energy = 0.0;
    for (qsizetype i = 0; i < count; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(count - 1));
        w[i] = float(block[i] * hann);
        energy += double(w[i]) * w[i];
    }
    if (energy < 1e-12)
        return 0.0f;
    // A Hann window spreads a tone over its bin and the two neighbours; together
    // they hold all of its energy (Parseval: sum |X|^2 = N * sum x^2, both sides).
    const double spacing = double(sampleRate) / double(count);
    const double power = goertzelPower(w, freq - spacing, sampleRate) + goertzelPower(w, freq, sampleRate)
        + goertzelPower(w, freq + spacing, sampleRate);
    return float(std::clamp(2.0 * power / (double(count) * energy), 0.0, 1.0));
}

void RoutingCheck::Detector::feed(const float *samples, qsizetype count)
{
    for (qsizetype i = 0; i < count;) {
        const qsizetype n = std::min(count - i, m_blockLen - m_block.size());
        AudioConvert::append(m_block, samples + i, n);
        i += n;
        if (m_block.size() == m_blockLen) {
            analyse(m_block.constData(), m_block.size());
            m_block.clear();
        }
    }
}

void RoutingCheck::Detector::analyse(const float *block, qsizetype count)
{
    const float rms = AudioConvert::rms(block, count);
    m_level = meterLevel(rms);
    m_loudest = std::max(m_loudest, m_level);
    if (rms < kMinRms)
        return;
    if (toneShare(block, count, ToneA, m_rate) > kToneShare)
        ++m_hitsA;
    else if (toneShare(block, count, ToneB, m_rate) > kToneShare)
        ++m_hitsB;
}

bool RoutingCheck::Detector::heard() const
{
    return m_hitsA >= kHitsNeeded && m_hitsB >= kHitsNeeded;
}

QVector<float> RoutingCheck::testSignal(int sampleRate)
{
    const int rate = sampleRate > 0 ? sampleRate : kSignalRate;
    const qsizetype toneLen = qsizetype(rate) * kToneMs / 1000;
    const qsizetype fade = rate / 100; // 10 ms
    QVector<float> out(toneLen * 3, 0.0f);
    const double freqs[3] = {ToneA, ToneB, ToneA};
    for (int t = 0; t < 3; ++t) {
        for (qsizetype i = 0; i < toneLen; ++i) {
            float env = 1.0f;
            if (i < fade)
                env = float(i) / float(fade);
            else if (toneLen - i < fade)
                env = float(toneLen - i) / float(fade);
            out[t * toneLen + i] = 0.3f * env * float(std::sin(2.0 * kPi * freqs[t] * double(i) / rate));
        }
    }
    return out;
}

// --- Device pairing ------------------------------------------------------------------

int RoutingCheck::pairedInputIndex(const QString &output, const QStringList &inputs)
{
    const auto find = [&inputs](auto pred) -> int {
        for (int i = 0; i < inputs.size(); ++i) {
            if (pred(inputs.at(i)))
                return i;
        }
        return -1;
    };
    const auto named = [&find](const QString &name) {
        return find([&name](const QString &in) { return in.compare(name, Qt::CaseInsensitive) == 0; });
    };
    const auto containing = [&find](const QString &part) {
        return find([&part](const QString &in) { return in.contains(part, Qt::CaseInsensitive); });
    };
    const auto starting = [&find](const QString &prefix) {
        return find([&prefix](const QString &in) { return in.startsWith(prefix, Qt::CaseInsensitive); });
    };

    if (output.contains(QLatin1String("Vocal Ink Voice"), Qt::CaseInsensitive)) {
        // Linux: our null sink; its remap source, or else its monitor.
        const int remap = containing(QStringLiteral("Vocal Ink Microphone"));
        return remap >= 0 ? remap : containing(QStringLiteral("Monitor of Vocal Ink Voice"));
    }
    if (output.contains(QLatin1String("Vocal Ink Virtual Mic"), Qt::CaseInsensitive)
        || output.contains(QLatin1String("Vocal Ink Speaker"), Qt::CaseInsensitive)) {
        return containing(QStringLiteral("Vocal Ink Virtual Mic")); // bundled driver
    }
    if (output.contains(QLatin1String("BlackHole"), Qt::CaseInsensitive)
        || output.contains(QLatin1String("Loopback"), Qt::CaseInsensitive)) {
        return named(output); // loopback drivers use the same name both ways
    }
    // VB-CABLE (CABLE, CABLE-A...), Hi-Fi Cable and VoiceMeeter: "X Input" plays into "X Output".
    const bool vbAudio = output.contains(QLatin1String("CABLE"), Qt::CaseInsensitive)
        || output.contains(QLatin1String("VoiceMeeter"), Qt::CaseInsensitive)
        || output.contains(QLatin1String("VB-Audio"), Qt::CaseInsensitive);
    const qsizetype at = output.indexOf(QLatin1String("Input"), 0, Qt::CaseInsensitive);
    if (vbAudio && at > 0) {
        QString swapped = output;
        swapped.replace(at, 5, QStringLiteral("Output"));
        int i = named(swapped);
        if (i >= 0)
            return i;
        const QString prefix = output.left(at).trimmed();
        i = starting(prefix + QStringLiteral(" Output"));
        if (i >= 0)
            return i;
        return starting(prefix + QStringLiteral(" Out ")); // e.g. "Voicemeeter Out B1"
    }
    return -1;
}

QByteArray RoutingCheck::pairedInputFor(const QByteArray &outputDeviceId)
{
    QString output;
    if (outputDeviceId.isEmpty()) {
        output = QMediaDevices::defaultAudioOutput().description();
    } else {
        const auto outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &dev : outputs) {
            if (dev.id() == outputDeviceId) {
                output = dev.description();
                break;
            }
        }
    }
    if (output.isEmpty())
        return {};
    const auto inputs = QMediaDevices::audioInputs();
    QStringList names;
    for (const QAudioDevice &dev : inputs)
        names << dev.description();
    const int i = pairedInputIndex(output, names);
    return i >= 0 ? inputs.at(i).id() : QByteArray();
}

// --- The check -------------------------------------------------------------------------

class RoutingCheck::Private
{
public:
    bool running = false;
    int generation = 0;
    QByteArray outputId;
    QByteArray inputId;
    int timeoutMs = 4000;
    QString outputName;
    QString inputName;
    QtAudioLane *lane = nullptr;
    QAudioSource *source = nullptr;
    QIODevice *io = nullptr;
    QAudioFormat format;
    QByteArray leftover;
    Detector detector;
    QTimer *timeout = nullptr;
    QTimer *grace = nullptr;
    bool toneDone = false;
    bool extended = false;
};

RoutingCheck::RoutingCheck(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->timeout = new QTimer(this);
    d->timeout->setSingleShot(true);
    d->grace = new QTimer(this);
    d->grace->setSingleShot(true);
    connect(d->grace, &QTimer::timeout, this, &RoutingCheck::conclude);
    connect(d->timeout, &QTimer::timeout, this, [this] {
        // Slow devices can take a while to start: give a tone that is still
        // playing one more period before giving up.
        if (d->running && !d->toneDone && !d->extended) {
            d->extended = true;
            d->timeout->start(d->timeoutMs);
            return;
        }
        conclude();
    });
}

RoutingCheck::~RoutingCheck()
{
    teardown();
    delete d;
}

bool RoutingCheck::isRunning() const
{
    return d->running;
}

void RoutingCheck::cancel()
{
    if (!d->running)
        return;
    d->running = false;
    ++d->generation;
    teardown();
}

void RoutingCheck::teardown()
{
    d->timeout->stop();
    d->grace->stop();
    if (d->source) {
        d->source->disconnect(this);
        d->source->stop();
        d->source->deleteLater();
        d->source = nullptr;
    }
    d->io = nullptr;
    if (d->lane) {
        d->lane->disconnect(this);
        d->lane->stopAllSounds();
        d->lane->deleteLater();
        d->lane = nullptr;
    }
    d->leftover.clear();
}

void RoutingCheck::finish(bool heard, const QString &detail)
{
    if (!d->running)
        return;
    d->running = false;
    ++d->generation;
    teardown();
    emit finished(heard, detail);
}

void RoutingCheck::finishLater(const QString &detail)
{
    const int gen = d->generation;
    QTimer::singleShot(0, this, [this, gen, detail] {
        if (gen == d->generation)
            finish(false, detail);
    });
}

void RoutingCheck::start(const QByteArray &outputDevice, const QByteArray &inputDevice, int timeoutMs)
{
    cancel();
    d->running = true;
    d->outputId = outputDevice;
    d->inputId = inputDevice;
    d->timeoutMs = std::max(1500, timeoutMs);

    QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (!outputDevice.isEmpty()) {
        output = QAudioDevice();
        const auto outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &dev : outputs) {
            if (dev.id() == outputDevice)
                output = dev;
        }
    }
    if (output.isNull()) {
        finishLater(tr("The voice output device isn't connected. Pick it again in Audio & mic."));
        return;
    }
    d->outputName = output.description();

    const QByteArray inputId = inputDevice.isEmpty() ? pairedInputFor(output.id()) : inputDevice;
    if (inputId.isEmpty()) {
        finishLater(tr("Vocal Ink can't tell which microphone belongs to “%1”. Choose it in the list and try again.")
                        .arg(d->outputName));
        return;
    }
    QAudioDevice input;
    const auto inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice &dev : inputs) {
        if (dev.id() == inputId)
            input = dev;
    }
    if (input.isNull()) {
        finishLater(tr("The device to listen on isn't connected."));
        return;
    }
    d->inputName = input.description();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Listening needs microphone access on macOS.
    QMicrophonePermission permission;
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined: {
        const int gen = d->generation;
        qApp->requestPermission(permission, this, [this, gen](const QPermission &p) {
            if (gen != d->generation || !d->running)
                return;
            if (p.status() == Qt::PermissionStatus::Granted)
                start(d->outputId, d->inputId, d->timeoutMs);
            else
                finish(false, tr("Microphone access was denied. Allow Vocal Ink in your system's privacy settings."));
        });
        return;
    }
    case Qt::PermissionStatus::Denied:
        finishLater(tr("Microphone access was denied. Allow Vocal Ink in your system's privacy settings."));
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif

    QAudioFormat format = input.preferredFormat();
    if (!format.isValid() || format.sampleFormat() == QAudioFormat::Unknown) {
        format.setSampleRate(kSignalRate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
    }
    d->format = format;
    d->detector = Detector(format.sampleRate());
    d->toneDone = false;
    d->extended = false;
    d->leftover.clear();
    d->source = new QAudioSource(input, format, this);
    d->source->setBufferSize(format.bytesForDuration(50000));
    d->io = d->source->start();
    if (!d->io || d->source->error() != QAudio::NoError) {
        finishLater(tr("Couldn't open “%1” to listen.").arg(d->inputName));
        return;
    }
    connect(d->io, &QIODevice::readyRead, this, &RoutingCheck::onCaptured);

    d->lane = new QtAudioLane(output.id(), this);
    connect(d->lane, &AudioOutputLane::errorOccurred, this, [this](const QString &message) { finish(false, message); });
    connect(d->lane, &AudioOutputLane::soundFinished, this, [this] {
        d->toneDone = true;
        d->grace->start(kGraceMs);
    });
    const int gen = d->generation;
    QTimer::singleShot(kLeadInMs, this, [this, gen] {
        if (gen == d->generation && d->running && d->lane)
            d->lane->playSound(kToneSoundId, testSignal(kSignalRate), kSignalRate, 1.0f);
    });
    d->timeout->start(d->timeoutMs);
}

void RoutingCheck::onCaptured()
{
    if (!d->io || !d->running)
        return;
    QByteArray data = d->leftover + d->io->readAll();
    const int frame = d->format.bytesPerFrame();
    if (frame <= 0)
        return;
    const qsizetype usable = data.size() - data.size() % frame;
    d->leftover = data.mid(usable);
    data.truncate(usable);
    if (data.isEmpty())
        return;
    const QVector<float> mono = AudioConvert::toMonoFloat(data, d->format);
    d->detector.feed(mono.constData(), mono.size());
    emit progress(d->detector.level());
    if (d->detector.heard())
        finish(true, tr("Your voice reaches “%1”.").arg(d->inputName));
}

void RoutingCheck::conclude()
{
    if (!d->running)
        return;
    if (d->detector.heard()) {
        finish(true, tr("Your voice reaches “%1”.").arg(d->inputName));
    } else if (d->detector.loudestLevel() >= kSomethingLevel) {
        finish(false, tr("Sound arrived on “%1”, but not Vocal Ink's test tone. Is it really the other end of “%2”?")
                          .arg(d->inputName, d->outputName));
    } else {
        finish(false, tr("Nothing arrived on “%1”. Check that “%2” isn't muted or disabled in your system's sound settings.")
                          .arg(d->inputName, d->outputName));
    }
}
