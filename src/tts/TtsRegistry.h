#pragma once

#include "tts/TtsEngine.h"

#include <QList>
#include <QObject>

// Owns all TTS engines and answers "which voices exist?" across providers.
class TtsRegistry : public QObject
{
    Q_OBJECT
public:
    explicit TtsRegistry(QObject *parent = nullptr);

    void addEngine(TtsEngine *engine); // takes ownership
    QList<TtsEngine *> engines() const { return m_engines; }
    TtsEngine *engine(const QString &id) const;

    QList<Voice> allVoices() const;
    // Full voice info if the engine has listed it; otherwise Voice::fromKey(key).
    Voice resolve(const QString &key) const;
    bool isUsable(const Voice &voice) const;
    // First usable voice, preferring local engines (Piper, then system voices).
    Voice fallbackVoice() const;

    void refreshAll();

signals:
    void voicesChanged();
    void engineAvailabilityChanged(TtsEngine *engine);

private:
    QList<TtsEngine *> m_engines;
};
