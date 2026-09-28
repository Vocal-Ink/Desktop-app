# Third-party software

Vocal Ink is licensed under the Apache License 2.0 (see `LICENSE`). It is built
with, or downloads at the user's request, the following third-party software.

## Bundled with the app

| Component | License | Notes |
|---|---|---|
| [Qt 6](https://www.qt.io/) (Core, Gui, Widgets, Qml, Quick, Quick Controls, Quick Shapes, Network, Multimedia, TextToSpeech, WebSockets, Concurrent) | LGPL-3.0 | Dynamically linked. You may replace the Qt libraries shipped with Vocal Ink with your own build. Source: https://download.qt.io/official_releases/qt/ |
| [FFmpeg](https://ffmpeg.org/) (shipped by Qt Multimedia) | LGPL-2.1+ | Dynamically linked, as distributed by the Qt project. |
| [whisper.cpp / ggml](https://github.com/ggml-org/whisper.cpp) v1.9.4 | MIT | Statically linked on-device speech recognition. Copyright (c) 2023-2026 The ggml authors. |
| [QHotkey](https://github.com/Skycoder42/QHotkey) 1.5.0 | BSD-3-Clause | System-wide shortcuts. Copyright (c) 2016 Felix Barz. |
| [QtKeychain](https://github.com/frankosterfeld/qtkeychain) 0.17.0 | BSD-3-Clause | Secure storage of API keys. Copyright (c) 2011-2026 Frank Osterfeld and contributors. |
| [Bricolage Grotesque](https://github.com/ateliertriay/bricolage) | OFL-1.1 | Display and stage type. Static instances in `resources/fonts` (see `LICENSES.md` there). |
| [Atkinson Hyperlegible Next & Mono](https://github.com/googlefonts/atkinson-hyperlegible-next) | OFL-1.1 | Interface text and keycaps. © Braille Institute of America. |
| [Lexend](https://github.com/googlefonts/lexend) | OFL-1.1 | Optional reading font. |
| [OpenDyslexic](https://github.com/antijingoist/opendyslexic) | OFL-1.1 | Optional reading font, unmodified. |
| [Lucide icons](https://lucide.dev) | ISC | Interface icons, converted to path data in `src/qml/Icons.js`. |
| [wordfreq](https://github.com/rspeer/wordfreq) word list | CC BY-SA 4.0 | Base vocabulary for word prediction in `resources/predict/`. By Robyn Speer; filtered and reformatted. |

## Downloaded on request (not bundled)

| Component | License | Notes |
|---|---|---|
| [Piper](https://github.com/rhasspy/piper) 2023.11.14-2 runtime | MIT | Local neural text-to-speech engine, downloaded from GitHub releases when you choose to install it. Includes espeak-ng data (GPL-3.0) and ONNX Runtime (MIT) as distributed by the Piper project. |
| [Piper voices](https://huggingface.co/rhasspy/piper-voices) | Varies per voice | Each voice has its own license, listed in its `MODEL_CARD` on Hugging Face. |
| [Whisper models](https://huggingface.co/ggerganov/whisper.cpp) (ggml format) | MIT | OpenAI Whisper weights converted for whisper.cpp. |

## Optional external programs

Vocal Ink can use these if they are installed on your system; they are not
distributed with it:

- [eSpeak NG](https://github.com/espeak-ng/espeak-ng) (GPL-3.0), run as a separate process.
- [VB-CABLE](https://vb-audio.com/Cable/) (donationware), [BlackHole](https://github.com/ExistentialAudio/BlackHole) (GPL-3.0) or PulseAudio/PipeWire virtual devices for routing audio to other apps.

## Cloud services

Microsoft Azure Speech, ElevenLabs, Fish Audio and OpenAI are used only when you
add your own API key. Text you choose to speak with a cloud voice (and audio you
choose to transcribe with a cloud recognizer) is sent to that provider under
their terms of service.
