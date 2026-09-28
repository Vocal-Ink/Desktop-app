#pragma once

#include "tts/TtsEngine.h"

#include <QNetworkRequest>

// ElevenLabs streaming text-to-speech (raw 24 kHz PCM). Lists the account's
// voice library and voice ids the user pasted in.
class ElevenLabsTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit ElevenLabsTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("elevenlabs"); }
    QString displayName() const override { return QStringLiteral("ElevenLabs"); }
    bool isLocal() const override { return false; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    bool supportsCustomVoiceIds() const override { return true; }
    void addCustomVoice(const QString &id, const QString &name) override;
    void removeCustomVoice(const QString &id) override;

private:
    QNetworkRequest request(const QString &pathAndQuery) const;
    void fetchPage(int generation, int page, const QString &pageToken);
    void publishVoices();

    QList<Voice> m_remote;  // voices from the last successful listing
    QList<Voice> m_loading; // pages collected by the refresh in progress
    int m_refreshGeneration = 0;
};
