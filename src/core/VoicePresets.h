#pragma once

#include <QByteArray>
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

    bool operator==(const VoicePreset &o) const
    {
        return id == o.id && name == o.name && voiceKey == o.voiceKey && rate == o.rate && pitch == o.pitch
               && effect == o.effect && effectIntensity == o.effectIntensity;
    }
};

class VoicePresets : public QObject
{
    Q_OBJECT
public:
    explicit VoicePresets(const QString &filePath, QObject *parent = nullptr);

    bool load(); // an empty list if the file doesn't exist yet
    bool save() const;

    const QList<VoicePreset> &presets() const { return m_presets; }
    VoicePreset preset(const QString &id) const;
    QString add(const VoicePreset &preset); // returns the new id
    void update(const VoicePreset &preset);
    void remove(const QString &id);
    void move(int from, int to);

    static QByteArray toJson(const QList<VoicePreset> &presets);
    static QList<VoicePreset> fromJson(const QByteArray &json, bool *ok = nullptr);

signals:
    void changed();

private:
    void commit();
    QString newId() const;

    QString m_path;
    QList<VoicePreset> m_presets;
};
