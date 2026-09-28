#include "tts/TtsRegistry.h"

#include <QLocale>

TtsRegistry::TtsRegistry(QObject *parent)
    : QObject(parent)
{
}

void TtsRegistry::addEngine(TtsEngine *engine)
{
    engine->setParent(this);
    m_engines.append(engine);
    connect(engine, &TtsEngine::voicesChanged, this, &TtsRegistry::voicesChanged);
    connect(engine, &TtsEngine::availabilityChanged, this, [this, engine] {
        emit engineAvailabilityChanged(engine);
        emit voicesChanged();
    });
}

TtsEngine *TtsRegistry::engine(const QString &id) const
{
    for (TtsEngine *e : m_engines) {
        if (e->id() == id)
            return e;
    }
    return nullptr;
}

QList<Voice> TtsRegistry::allVoices() const
{
    QList<Voice> all;
    for (TtsEngine *e : m_engines) {
        if (e->isAvailable())
            all += e->voices();
    }
    return all;
}

Voice TtsRegistry::resolve(const QString &key) const
{
    const Voice minimal = Voice::fromKey(key);
    if (TtsEngine *e = engine(minimal.engineId)) {
        const QList<Voice> voices = e->voices();
        for (const Voice &v : voices) {
            if (v.id == minimal.id)
                return v;
        }
    }
    return minimal;
}

bool TtsRegistry::isUsable(const Voice &voice) const
{
    TtsEngine *e = engine(voice.engineId);
    return e && e->isAvailable() && voice.isValid();
}

Voice TtsRegistry::fallbackVoice() const
{
    for (const auto *id : {"piper", "system", "espeak"}) {
        if (TtsEngine *e = engine(QLatin1String(id)); e && e->isAvailable() && !e->voices().isEmpty()) {
            // Prefer a voice in the user's language.
            const QString lang = QLocale().name().replace(QLatin1Char('_'), QLatin1Char('-'));
            const QList<Voice> voices = e->voices();
            for (const Voice &v : voices) {
                if (v.language.compare(lang, Qt::CaseInsensitive) == 0)
                    return v;
            }
            return voices.first();
        }
    }
    for (TtsEngine *e : m_engines) {
        if (e->isAvailable() && !e->voices().isEmpty())
            return e->voices().first();
    }
    return {};
}

void TtsRegistry::refreshAll()
{
    for (TtsEngine *e : m_engines) {
        if (e->isAvailable())
            e->refreshVoices();
    }
}
