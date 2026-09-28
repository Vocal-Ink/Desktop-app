#pragma once

#include "tts/TtsEngine.h"

#include <QList>

// Creates every TTS engine the app offers, in display order:
//   piper, system, espeak, azure, elevenlabs, fishaudio, openai
// Engines that can never work on this platform (e.g. no Qt synthesize support)
// are still created but report isAvailable() == false with a reason.
QList<TtsEngine *> createTtsEngines(const EngineContext &context, QObject *parent);
