#pragma once

#include <QList>
#include <QObject>
#include <QString>

// A named bundle of how you sound: voice + speed + pitch + effect. Presets 1–3
// can be switched with shortcuts ("preset.1" .. "preset.3").
struct VoicePreset
{
    QString id;           // stable identifier
    QString name;         // e.g. "Chill stream", "Work calls"
    QString voiceKey;     // Voice::key()
    int rate = 100;       // % (50..200)
    int pitch = 0;        // -50..50
    QString effect;       // VoiceEffects::id()
    int effectIntensity = 60; // %
};

class VoicePresets : public QObject
{
    Q_OBJECT
public:
    explicit VoicePresets(const QString &filePath, QObject *parent = nullptr);

    bool load();
    bool save() const;

    const QList<VoicePreset> &presets() const { return m_presets; }
    VoicePreset preset(const QString &id) const;
    QString add(const VoicePreset &preset); // returns the new id
    void update(const VoicePreset &preset);
    void remove(const QString &id);
    void move(int from, int to);

signals:
    void changed();

private:
    QString m_path;
    QList<VoicePreset> m_presets;
};
