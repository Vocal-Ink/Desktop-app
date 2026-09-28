#pragma once

#include "stt/SttEngine.h"
#include "tts/TtsEngine.h" // EngineContext

#include <QHash>
#include <QPointer>

class QNetworkReply;

// Cloud recognition through an OpenAI-compatible POST {base}/audio/transcriptions
// (OpenAI, Groq, a local faster-whisper server, ...). The key is Secrets::OpenAiStt,
// falling back to the text-to-speech Secrets::OpenAi key.
class OpenAiSttEngine : public SttEngine
{
    Q_OBJECT
public:
    explicit OpenAiSttEngine(const EngineContext &context, QObject *parent = nullptr);
    ~OpenAiSttEngine() override;

    QString id() const override { return QStringLiteral("openai"); }
    QString displayName() const override { return tr("OpenAI-compatible (cloud)"); }
    bool isLocal() const override { return false; }
    bool isReady() const override;
    QString notReadyReason() const override;

    void transcribe(quint64 requestId, const QVector<float> &mono16k) override;

private:
    QString apiKey() const;
    QString baseUrl() const;
    QString providerName() const;
    void onFinished(quint64 requestId, QNetworkReply *reply);
    void failLater(quint64 requestId, const QString &error);

    EngineContext m_ctx;
    QHash<quint64, QPointer<QNetworkReply>> m_replies;
    bool m_lastReady = false;
};
