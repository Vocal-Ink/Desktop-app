#include "tts/Engines.h"

#include "tts/AzureTtsEngine.h"
#include "tts/ElevenLabsTtsEngine.h"
#include "tts/EspeakTtsEngine.h"
#include "tts/FishAudioTtsEngine.h"
#include "tts/OpenAiTtsEngine.h"
#include "tts/PiperTtsEngine.h"
#include "tts/SystemTtsEngine.h"

QList<TtsEngine *> createTtsEngines(const EngineContext &context, QObject *parent)
{
    return {
        new PiperTtsEngine(context, parent),
        new SystemTtsEngine(context, parent),
        new EspeakTtsEngine(context, parent),
        new AzureTtsEngine(context, parent),
        new ElevenLabsTtsEngine(context, parent),
        new FishAudioTtsEngine(context, parent),
        new OpenAiTtsEngine(context, parent),
    };
}
