#include "tts/TtsEngine.h"

#include <QTimer>

TtsStream::TtsStream(QObject *parent)
    : QObject(parent)
{
}

void TtsStream::cancel()
{
    if (m_cancelled || m_done)
        return;
    m_cancelled = true;
    onCancel();
}

void TtsStream::deliverAudio(const QAudioFormat &format, const QByteArray &pcm)
{
    if (m_cancelled || m_done || pcm.isEmpty())
        return;
    emit audioReady(format, pcm);
}

void TtsStream::deliverFinished()
{
    if (m_cancelled || m_done)
        return;
    m_done = true;
    emit finished();
}

void TtsStream::deliverFailed(const QString &error)
{
    if (m_cancelled || m_done)
        return;
    m_done = true;
    emit failed(error);
}

void TtsStream::deliverFailedLater(const QString &error)
{
    QTimer::singleShot(0, this, [this, error] { deliverFailed(error); });
}

TtsEngine::TtsEngine(const EngineContext &context, QObject *parent)
    : QObject(parent)
    , m_ctx(context)
{
}

void TtsEngine::setVoices(const QList<Voice> &voices)
{
    m_voices = voices;
    m_refreshing = false;
    emit voicesChanged();
}
