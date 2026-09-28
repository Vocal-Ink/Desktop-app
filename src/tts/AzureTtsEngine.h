#pragma once

#include "tts/TtsEngine.h"

#include <QNetworkRequest>

// Microsoft Azure AI Speech over REST: SSML in, raw 24 kHz 16-bit mono PCM out.
class AzureTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit AzureTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("azure"); }
    QString displayName() const override { return QStringLiteral("Microsoft Azure"); }
    bool isLocal() const override { return false; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    // The SSML document sent for one request.
    static QString buildSsml(const QString &text, const QString &voiceName, const QString &locale,
                             const SpeakOptions &options);

private:
    QString region() const;
    QNetworkRequest request(const QString &path) const;

    int m_refreshGeneration = 0;
};
