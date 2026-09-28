<p align="center"><img src="resources/icons/app-256.png" width="96" alt=""></p>

<h1 align="center">Vocal Ink</h1>

<p align="center"><b>Your words, in a voice you choose — in calls, games and on stream.</b></p>

Vocal Ink is a free desktop app that speaks for people who can't, or don't want to, use their own voice.
**Type** a message, or **speak** (even quietly or unclearly) and let it be written down for you, and Vocal Ink
says it aloud in the voice you pick. Other apps hear it as a **microphone**, so it works in Discord, Zoom,
games and OBS, and it can put **live captions on your stream**.

It is a native C++/Qt 6 app for **Windows, macOS and Linux**. Everything works **offline and for free** with
local voices and on-device speech recognition; cloud voices are optional and use your own API keys.

---

## Features

**Talking**
- Type and press <kbd>Enter</kbd> — long messages start speaking after the first sentence, so there's no wait.
- **Quick phrases**: one click (or <kbd>Alt</kbd>+<kbd>1…9</kbd>, or your own system-wide shortcut) for "Yes", "No", "One moment, I'm typing"…
- **History**: double-click to say something again; <kbd>↑</kbd> recalls earlier messages; <kbd>Tab</kbd> completes words you use.
- **Text replacements**: `brb` → "be right back", or spell names the way they should sound.
- **Quick-type box** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>T</kbd>): a small box that pops up over games — type, <kbd>Enter</kbd>, gone.
- Stop, skip and repeat at any time (<kbd>Esc</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd>).

**Voices** — mix and match, favourite the ones you like, preview before you pick:

| Provider | Runs | Cost | Notes |
|---|---|---|---|
| **Piper** | on your computer | free | Natural neural voices in 40+ languages, downloaded in-app. |
| **System voices** | on your computer | free | Windows (SAPI/OneCore) and macOS voices. |
| **eSpeak NG** | on your computer | free | Robotic but tiny; 100+ languages. Used if installed. |
| **Microsoft Azure** | cloud | your key | 500+ neural voices, 140+ languages, speaking styles. |
| **ElevenLabs** | cloud | your key | Very natural voices, and your own cloned voice. |
| **Fish Audio** | cloud | your key | Huge community voice library (searchable in-app) and clones. |
| **OpenAI** | cloud | your key | `gpt-4o-mini-tts` with delivery instructions; any OpenAI-compatible server. |

**Speech recognition (speak instead of type)**
- On-device with [whisper.cpp](https://github.com/ggml-org/whisper.cpp) — private, free, offline. Pick a model size in-app.
- Or any OpenAI-compatible `/audio/transcriptions` service (OpenAI, Groq, self-hosted Speaches…).
- **Push-to-talk** (hold a button or <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Space</kbd> in any app), **toggle**, or **hands-free** with adjustable sensitivity for quiet or whispered speech.
- Review the text before it's spoken (default) or have it spoken right away. Add names and slang as vocabulary hints.

**Streaming (OBS)** — see [docs/OBS.md](docs/OBS.md)
- **Caption overlay**: a Browser Source with subtitles, speech-bubble or plain styles and word-by-word reveal. One click adds it to your scene.
- **Text-source subtitles** through obs-websocket (built into OBS 28+).
- **Closed captions** (CC) for Twitch/YouTube while live.
- **Talking indicator**: shows a source (e.g. a PNGtuber mouth) only while your voice plays.

**Accessibility**: full keyboard control, screen-reader labels, dark/light/high-contrast themes, adjustable text size, tray icon so shortcuts keep working, one instance at a time.

## Getting started

1. **Download** the latest build for your system from the [Releases](https://github.com/Vocal-Ink/Desktop-app/releases) page
   (Windows installer or portable zip, macOS `.dmg`, Linux `.AppImage`).
2. **Set up the virtual microphone** so other apps can hear Vocal Ink. Vocal Ink brings its own
   ([how it works](docs/VIRTUAL_AUDIO.md)): voice output → *Vocal Ink Voice*, and in Discord/OBS/Zoom pick
   *Vocal Ink Mic* as the microphone (macOS: *Vocal Ink Virtual Mic* for both).
   - **Windows:** tick *Install the Vocal Ink virtual microphone* in the installer, or install it from Vocal Ink's
     audio settings. Builds without the signed driver use [VB-CABLE](https://vb-audio.com/Cable/) (free) instead:
     voice output → *CABLE Input*, microphone → *CABLE Output*.
   - **macOS:** install it from Vocal Ink's audio settings (asks for your password). Or use
     [BlackHole 2ch](https://existential.audio/blackhole/) (`brew install blackhole-2ch`).
   - **Linux:** install it from Vocal Ink's audio settings (PulseAudio or PipeWire, no password).
3. **Open Vocal Ink.** The setup assistant helps you choose the output, download a free voice (~90 MB) and a speech model (~60 MB).
4. Type something and press <kbd>Enter</kbd>. Keep *Also play on my speakers* on to hear yourself.

> **macOS:** builds are signed ad hoc. The first time, right-click the app → **Open**. Allow microphone access when asked if you use speech recognition.
> **Linux (Wayland):** system-wide shortcuts need X11/XWayland — start with `QT_QPA_PLATFORM=xcb` to use them.

## Privacy

- Local voices and on-device recognition never send anything anywhere.
- Cloud voices/recognizers receive only the text/audio you choose to process with them, under that provider's terms.
- API keys are stored in your operating system's password manager (Windows Credential Manager, macOS Keychain, Secret Service/KWallet). If none is available they are kept in Vocal Ink's settings file, and the app tells you.
- The caption overlay server listens on `127.0.0.1` only, unless you allow LAN access for two-PC streaming.

## Building from source

Requirements: CMake ≥ 3.25, a C++17 compiler (MSVC 2022, Clang, GCC 11+), **Qt 6.5+** (6.8 LTS recommended) with
*Qt Multimedia*, *Qt TextToSpeech* and *Qt WebSockets*, and Git (dependencies are fetched automatically).
Linux also needs `libsecret-1-dev` and `libx11-dev`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/VocalInk        # macOS: open build/src/VocalInk.app
```

Useful options: `-DVOCALINK_WITH_WHISPER=OFF`, `-DVOCALINK_WITH_HOTKEYS=OFF`, `-DVOCALINK_WITH_KEYCHAIN=OFF`,
`-DVOCALINK_BUILD_TESTS=OFF`. Qt 6.4 also builds, but system voices need Qt 6.6+.
`-DVOCALINK_WITH_MAC_DRIVER=ON` also builds the macOS virtual mic (a Core Audio plug-in) into the app; the Windows
driver builds with MSBuild from `driver/windows`. See [docs/VIRTUAL_AUDIO.md](docs/VIRTUAL_AUDIO.md).

Packaging scripts (used by CI, see `.github/workflows/build.yml`):

| Platform | Command | Output |
|---|---|---|
| Windows | `pwsh packaging/windows/deploy.ps1 -BuildDir build -Version 0.1.0` | Inno Setup installer + portable zip |
| macOS | `packaging/macos/build-dmg.sh build 0.1.0` | drag-to-install `.dmg` |
| Linux | `packaging/linux/build-appimage.sh build 0.1.0` | `.AppImage` |

Command-line options: `--minimized` (start in the tray), `--no-wizard`, `--show wizard|voices|settings[:page]`,
`--screenshot <file>` (used by CI smoke tests). A `portable.txt` next to the executable keeps all data in a `data`
folder beside it.

### Project layout

```
src/app        AppContext: owns and wires all services
src/core       settings, secrets, phrases, history, text processing, speech queue
src/audio      format conversion, resampler, multi-device output, microphone, voice activity detection
src/tts        voice engines (Piper, system, eSpeak, Azure, ElevenLabs, Fish Audio, OpenAI)
src/stt        speech recognition (whisper.cpp, OpenAI-compatible) and the microphone controller
src/models     downloads of Whisper models, the Piper runtime and Piper voices
src/obs        obs-websocket client, OBS integration, caption overlay web server
src/platform   global hotkeys, virtual audio helpers, the virtual mic installer (VirtualDriver)
src/ui         Qt Widgets user interface
resources/     icons and the overlay web page
tests/         Qt Test suites (run with ctest)
packaging/     Windows, macOS and Linux packaging
driver/        the virtual mic drivers: Windows (VocalInkAudio.sys, MS-PL) and macOS (HAL plug-in)
```

## Contributing

Bug reports and pull requests are welcome — especially from people who use AAC or text-to-speech every day.
Please run `ctest` before sending a change.

## License

[Apache License 2.0](LICENSE), except the Windows driver in `driver/windows`, which is derived from a Microsoft
sample and stays under the [Microsoft Public License](driver/windows/LICENSE). Third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
