#include "stt/SttEngines.h"

#include "stt/OpenAiSttEngine.h"
#include "stt/WhisperEngine.h"

SttEngine *createSttEngine(const QString &id, const EngineContext &context, QObject *parent)
{
    const QStringList ids = availableSttEngineIds();
    const QString chosen = ids.contains(id) ? id : ids.value(0);
#ifdef VOCALINK_HAVE_WHISPER
    if (chosen == QLatin1String("whisper"))
        return new WhisperEngine(context, parent);
#endif
    Q_UNUSED(chosen)
    return new OpenAiSttEngine(context, parent);
}

QStringList availableSttEngineIds()
{
    QStringList ids;
#ifdef VOCALINK_HAVE_WHISPER
    ids << QStringLiteral("whisper");
#endif
    ids << QStringLiteral("openai");
    return ids;
}
