#pragma once

#include "tts/TtsEngine.h"

// eSpeak NG, run locally: robotic but tiny, fast and available for ~100 languages.
class EspeakTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit EspeakTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("espeak"); }
    QString displayName() const override { return tr("eSpeak NG (local)"); }
    bool isLocal() const override { return true; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    // espeak-ng (or espeak), or empty when neither is installed.
    static QString executablePath();
    // Parses the table printed by `espeak-ng --voices`.
    static QList<Voice> parseVoiceList(const QByteArray &output);

private:
    int m_refreshGeneration = 0;
};
