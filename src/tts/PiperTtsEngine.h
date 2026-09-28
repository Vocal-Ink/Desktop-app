#pragma once

#include "tts/TtsEngine.h"

#include <QHash>

// Piper neural voices, run locally: one piper process per request, raw PCM on stdout.
// Voices are <key>.onnx + <key>.onnx.json pairs inside Paths::piperVoicesDir().
class PiperTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit PiperTtsEngine(const EngineContext &context, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("piper"); }
    QString displayName() const override { return tr("Piper (local)"); }
    bool isLocal() const override { return true; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

    // The piper executable that would be used, or empty when none is installed.
    QString executablePath() const;

private:
    struct Model
    {
        QString onnxPath;
        QString configPath;
        int sampleRate = 22050;
        int speakerCount = 1;
        QHash<QString, int> speakers; // speaker name -> id
    };

    // Reads every installed voice; fills m_models.
    QList<Voice> scan();
    static QStringList findModelFiles(const QString &dir, int depth);

    QHash<QString, Model> m_models; // file base name -> model
    bool m_wasAvailable = false;
};
