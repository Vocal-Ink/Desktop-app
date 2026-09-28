#include "stt/SttController.h"

#include "audio/AudioConvert.h"
#include "audio/MicCapture.h"
#include "audio/Vad.h"
#include "core/TextProcessor.h"

namespace {
constexpr qsizetype kMinSamples = qsizetype(SttController::SampleRate) * SttController::MinRecordingMs / 1000;
constexpr qsizetype kMaxSamples = qsizetype(SttController::SampleRate) * SttController::MaxRecordingSeconds;
} // namespace

SttController::SttController(QObject *parent)
    : QObject(parent)
    , m_mic(new MicCapture(this))
    , m_vad(new Vad)
{
    connect(m_mic, &MicCapture::samples, this, &SttController::onSamples);
    connect(m_mic, &MicCapture::levelChanged, this, &SttController::levelChanged);
    connect(m_mic, &MicCapture::errorOccurred, this, [this](const QString &message) {
        if (m_listening)
            stop(false);
        emit errorOccurred(message);
    });

    m_vad->onActivityChanged = [this](bool active) {
        if (m_mode == Mode::HandsFree)
            emit speechActiveChanged(active);
    };
    m_vad->onUtterance = [this](const QVector<float> &utterance) { submit(utterance); };
}

SttController::~SttController()
{
    m_mic->disconnect(this);
    delete m_vad;
}

void SttController::setEngine(SttEngine *engine)
{
    if (m_engine == engine)
        return;
    if (m_engine)
        disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    clearPending(); // answers from the previous engine are no longer wanted
    if (!engine)
        return;

    connect(engine, &SttEngine::transcribed, this, [this, engine](quint64 requestId, const QString &text) {
        if (engine != m_engine || !m_pending.contains(requestId))
            return;
        finishRequest(requestId);
        const QString clean = TextProcessor::cleanTranscript(text);
        if (!clean.isEmpty())
            emit transcript(clean);
    });
    connect(engine, &SttEngine::failed, this, [this, engine](quint64 requestId, const QString &error) {
        if (engine != m_engine || !m_pending.contains(requestId))
            return;
        finishRequest(requestId);
        emit errorOccurred(error);
    });
    connect(engine, &QObject::destroyed, this, [this] {
        if (!m_engine)
            clearPending(); // its answers will never come
    });
}

void SttController::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    if (m_listening)
        stop(false);
    m_mode = mode;
}

void SttController::setInputDevice(const QByteArray &deviceId)
{
    m_mic->setDevice(deviceId);
}

void SttController::setVadSensitivity(int percent)
{
    m_vad->setSensitivity(percent);
}

QString SttController::unavailableReason() const
{
    if (!m_engine)
        return tr("Speech recognition is not set up. Choose a recogniser in Settings → Speech input.");
    if (m_engine->isReady())
        return {};
    const QString reason = m_engine->notReadyReason();
    return reason.isEmpty() ? tr("Speech recognition is not ready yet.") : reason;
}

void SttController::startListening()
{
    if (m_listening)
        return;
    const QString problem = unavailableReason();
    if (!problem.isEmpty()) {
        emit errorOccurred(problem);
        return;
    }
    m_recording.clear();
    m_vad->reset();
    if (!m_simulatedInput && !m_mic->start())
        return; // MicCapture reported why
    setListening(true);
    if (m_mode != Mode::HandsFree)
        emit speechActiveChanged(true);
}

void SttController::stopListening()
{
    if (m_listening)
        stop(true);
}

void SttController::toggleListening()
{
    if (m_listening)
        stopListening();
    else
        startListening();
}

void SttController::cancel()
{
    if (m_listening)
        stop(false);
    clearPending();
}

void SttController::stop(bool transcribe)
{
    if (!m_simulatedInput)
        m_mic->stop();
    QVector<float> audio;
    audio.swap(m_recording);
    setListening(false);
    if (m_mode == Mode::HandsFree) {
        if (transcribe)
            m_vad->flush(); // submits the utterance in progress
        m_vad->reset();
    } else {
        emit speechActiveChanged(false);
        if (transcribe)
            submit(audio);
    }
}

void SttController::onSamples(const QVector<float> &mono16k)
{
    if (!m_listening) {
        // The microphone came up late (e.g. after a permission prompt); nobody is listening.
        if (!m_simulatedInput) {
            QMetaObject::invokeMethod(this, [this] {
                if (!m_listening)
                    m_mic->stop();
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (m_mode == Mode::HandsFree) {
        m_vad->process(mono16k);
        return;
    }
    AudioConvert::append(m_recording, mono16k.constData(), mono16k.size());
    // Very long push-to-talk / toggle recordings are sent in pieces as they grow.
    while (m_recording.size() >= kMaxSamples) {
        submit(m_recording.mid(0, kMaxSamples));
        m_recording.remove(0, kMaxSamples);
    }
}

void SttController::submit(const QVector<float> &audio)
{
    if (audio.size() < kMinSamples)
        return;
    const QString problem = unavailableReason();
    if (!problem.isEmpty()) {
        emit errorOccurred(problem);
        return;
    }
    const quint64 requestId = m_nextRequest++;
    const bool wasBusy = isBusy();
    m_pending.insert(requestId);
    if (!wasBusy)
        emit busyChanged(true);
    m_engine->transcribe(requestId, audio);
}

void SttController::finishRequest(quint64 requestId)
{
    m_pending.remove(requestId);
    if (m_pending.isEmpty())
        emit busyChanged(false);
}

void SttController::clearPending()
{
    if (m_pending.isEmpty())
        return;
    m_pending.clear();
    emit busyChanged(false);
}

void SttController::setListening(bool listening)
{
    if (m_listening == listening)
        return;
    m_listening = listening;
    emit listeningChanged(listening);
}

SttController::Mode SttController::modeFromString(const QString &s)
{
    if (s == QLatin1String("toggle"))
        return Mode::Toggle;
    if (s == QLatin1String("vad"))
        return Mode::HandsFree;
    return Mode::PushToTalk;
}

QString SttController::modeToString(Mode m)
{
    switch (m) {
    case Mode::Toggle: return QStringLiteral("toggle");
    case Mode::HandsFree: return QStringLiteral("vad");
    case Mode::PushToTalk: break;
    }
    return QStringLiteral("ptt");
}
