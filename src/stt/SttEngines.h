#pragma once

#include "stt/SttEngine.h"
#include "tts/TtsEngine.h" // EngineContext

// Creates the recogniser named by `id` ("whisper" = local whisper.cpp,
// "openai" = OpenAI-compatible /audio/transcriptions). Engines read their own
// settings (model file, base URL, API key) and follow changes to them.
SttEngine *createSttEngine(const QString &id, const EngineContext &context, QObject *parent);
QStringList availableSttEngineIds();
