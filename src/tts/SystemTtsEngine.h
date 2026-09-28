#pragma once

#include "tts/TtsEngine.h"

#include <memory>

// The operating system's voices (SAPI/WinRT, AVSpeechSynthesizer, flite, ...)
// through QTextToSpeech::synthesize(). That API needs Qt 6.6; with older Qt
// the engine exists but reports itself unavailable.
class SystemTtsEngine : public TtsEngine
{
    Q_OBJECT
public:
    explicit SystemTtsEngine(const EngineContext &context, QObject *parent = nullptr);
    ~SystemTtsEngine() override;

    QString id() const override { return QStringLiteral("system"); }
    QString displayName() const override { return tr("System voices"); }
    bool isLocal() const override { return true; }
    bool isAvailable() const override;
    QString unavailableReason() const override;

    void refreshVoices() override;
    TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};
