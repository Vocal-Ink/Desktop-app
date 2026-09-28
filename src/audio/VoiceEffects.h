#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

// Real-time effects applied to the synthesized voice before it is played.
namespace VoiceEffects {

enum class Effect { None, Radio, Telephone, Robot, Echo, Cave, Underwater, Megaphone };

QList<Effect> all();
QString id(Effect effect);            // stable settings value, e.g. "radio"
Effect fromId(const QString &id);
QString displayName(Effect effect);   // "Radio", "Telephone"...
QString description(Effect effect);   // one line for the picker

// A mono effect chain. Not thread-safe; one instance per stream.
class Chain
{
public:
    Chain();
    ~Chain();
    Chain(const Chain &) = delete;
    Chain &operator=(const Chain &) = delete;

    // intensity 0..1 (0 = dry)
    void configure(Effect effect, float intensity, int sampleRate);
    Effect effect() const { return m_effect; }
    bool isActive() const { return m_effect != Effect::None && m_intensity > 0.0f; }
    void process(float *samples, qsizetype count);
    // Remaining echo/reverb tail after the input ends (may be empty).
    QVector<float> flushTail();
    void reset();

private:
    struct State;
    std::unique_ptr<State> m_state;
    Effect m_effect = Effect::None;
    float m_intensity = 0.0f;
    int m_rate = 0;
};

} // namespace VoiceEffects
