#include "core/VoicePresets.h"

// Placeholder implementation (replaced by the text work package).
VoicePresets::VoicePresets(const QString &filePath, QObject *parent) : QObject(parent), m_path(filePath) {}
bool VoicePresets::load() { return true; }
bool VoicePresets::save() const { return true; }
VoicePreset VoicePresets::preset(const QString &id) const
{
    for (const VoicePreset &p : m_presets) {
        if (p.id == id)
            return p;
    }
    return {};
}
QString VoicePresets::add(const VoicePreset &preset)
{
    VoicePreset p = preset;
    p.id = QString::number(m_presets.size() + 1);
    m_presets.append(p);
    emit changed();
    return p.id;
}
void VoicePresets::update(const VoicePreset &) {}
void VoicePresets::remove(const QString &) {}
void VoicePresets::move(int, int) {}
