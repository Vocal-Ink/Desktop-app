#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

// A voice offered by one of the TTS engines.
struct Voice
{
    QString engineId;    // "piper", "system", "espeak", "azure", "elevenlabs", "fishaudio", "openai"
    QString id;          // engine-specific identifier used for synthesis
    QString name;        // human readable
    QString language;    // BCP-47 where known, e.g. "en-US"
    QString gender;      // "Female" / "Male" / "" (unknown)
    QString description; // accent, style, category...
    QString previewUrl;  // optional audio sample (cloud catalogues)
    int speaker = -1;    // multi-speaker local models (Piper)

    bool isValid() const { return !engineId.isEmpty() && !id.isEmpty(); }
    QString key() const { return engineId + QLatin1Char(':') + id; }

    // Rebuilds a minimal Voice from key() when the full catalogue isn't loaded yet.
    static Voice fromKey(const QString &key)
    {
        Voice v;
        const qsizetype colon = key.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            return v;
        v.engineId = key.left(colon);
        v.id = key.mid(colon + 1);
        v.name = v.id;
        return v;
    }

    bool operator==(const Voice &o) const { return engineId == o.engineId && id == o.id; }
};

Q_DECLARE_METATYPE(Voice)

// Per-message delivery options. Engines map these onto whatever their API supports.
struct SpeakOptions
{
    double rate = 1.0;     // 0.5 .. 2.0, 1.0 = normal speed
    double pitch = 0.0;    // -1.0 .. 1.0, 0 = voice default (not every engine supports it)
    QString instructions;  // free-form delivery hints (OpenAI gpt-4o-mini-tts "instructions")
};
