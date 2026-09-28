#pragma once

#include "tts/TtsEngine.h"

#include <QNetworkRequest>

class QJsonDocument;

// Fish Audio text-to-speech (streamed WAV). Lists the user's own models plus
// popular community voices, and searches the full catalogue by title.
class FishAudioTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit FishAudioTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("fishaudio"); }
    QString displayName() const override { return QStringLiteral("Fish Audio"); }
    bool isLocal() const override { return false; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    bool supportsRemoteSearch() const override { return true; }
    void searchVoices(const QString &query) override;

    bool supportsCustomVoiceIds() const override { return true; }
    void addCustomVoice(const QString &id, const QString &name) override;
    void removeCustomVoice(const QString &id) override;

private:
    QNetworkRequest request(const QString &pathAndQuery) const;
    QList<Voice> parseModels(const QJsonDocument &json, bool own) const;
    void publishVoices();

    QList<Voice> m_own;     // the account's models
    QList<Voice> m_popular; // most used community models
    int m_refreshGeneration = 0;
};
