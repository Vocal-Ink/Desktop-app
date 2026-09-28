#pragma once

#include "tts/TtsEngine.h"

// OpenAI /audio/speech (raw 24 kHz PCM). The base URL is configurable, so any
// OpenAI-compatible speech server works too.
class OpenAiTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit OpenAiTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("openai"); }
    QString displayName() const override { return QStringLiteral("OpenAI"); }
    bool isLocal() const override { return false; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    static QList<Voice> builtInVoices();

private:
    QString apiBase() const;
    bool usesCustomServer() const;
};
