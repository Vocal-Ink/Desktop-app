#include "audio/VoiceEffects.h"

#include <QCoreApplication>

// Placeholder implementation (replaced by the audio work package).
namespace VoiceEffects {

struct Chain::State {};

QList<Effect> all()
{
    return {Effect::None, Effect::Radio, Effect::Telephone, Effect::Robot, Effect::Echo,
            Effect::Cave, Effect::Underwater, Effect::Megaphone};
}

QString id(Effect effect)
{
    switch (effect) {
    case Effect::Radio: return QStringLiteral("radio");
    case Effect::Telephone: return QStringLiteral("telephone");
    case Effect::Robot: return QStringLiteral("robot");
    case Effect::Echo: return QStringLiteral("echo");
    case Effect::Cave: return QStringLiteral("cave");
    case Effect::Underwater: return QStringLiteral("underwater");
    case Effect::Megaphone: return QStringLiteral("megaphone");
    case Effect::None: break;
    }
    return QStringLiteral("none");
}

Effect fromId(const QString &value)
{
    for (Effect e : all()) {
        if (id(e) == value)
            return e;
    }
    return Effect::None;
}

QString displayName(Effect effect)
{
    switch (effect) {
    case Effect::Radio: return QCoreApplication::translate("VoiceEffects", "Radio");
    case Effect::Telephone: return QCoreApplication::translate("VoiceEffects", "Telephone");
    case Effect::Robot: return QCoreApplication::translate("VoiceEffects", "Robot");
    case Effect::Echo: return QCoreApplication::translate("VoiceEffects", "Echo");
    case Effect::Cave: return QCoreApplication::translate("VoiceEffects", "Cave");
    case Effect::Underwater: return QCoreApplication::translate("VoiceEffects", "Underwater");
    case Effect::Megaphone: return QCoreApplication::translate("VoiceEffects", "Megaphone");
    case Effect::None: break;
    }
    return QCoreApplication::translate("VoiceEffects", "No effect");
}

QString description(Effect effect)
{
    Q_UNUSED(effect)
    return {};
}

Chain::Chain() = default;
Chain::~Chain() = default;
void Chain::configure(Effect effect, float intensity, int sampleRate)
{
    m_effect = effect;
    m_intensity = intensity;
    m_rate = sampleRate;
}
void Chain::process(float *, qsizetype) {}
QVector<float> Chain::flushTail() { return {}; }
void Chain::reset() {}

} // namespace VoiceEffects
