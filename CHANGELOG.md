# Changelog

## 1.0.0 — 2026-09-29

The first release of Vocal Ink: a free app that speaks for people who can't, or don't want to, use their own
voice. Type, or speak quietly and let it be written down, and Vocal Ink says it aloud in the voice you choose.
Other apps hear it as a microphone, so it works in Discord, Zoom, games and OBS.

### Talking
- Type and press Enter. The spoken line fills with ink word by word so people nearby can follow along.
- Word suggestions that learn from you (on your computer only), abbreviations (`brb` → "be right back"),
  variables like `{time}` and `{clipboard}`, and a choice of how emoji and links are read.
- A **board** of phrase tiles and a **soundboard** that plays into your virtual mic, with switch-access scanning.
- A quick-type box over any app or game, a compact bar, and a full-screen text view you can flip to face someone.

### Voices
- Free voices on your computer (Piper, system voices, eSpeak NG) and cloud voices from Microsoft Azure,
  ElevenLabs, Fish Audio and OpenAI, with your own API keys.
- Favourites, presets (voice, speed, pitch and effect) and voice effects (radio, telephone, robot, echo and more).
- The recommended voice and speech model follow your language.

### Dictation
- Speak instead of typing, on your computer with whisper.cpp or with an OpenAI-compatible service.
  Hold to dictate, tap on and off, or hands-free.

### Microphone
- Hear yourself with separate volumes and sound cues only you hear.
- Mix in your real microphone while you hold a key, with a toggle, or always. Off by default, and a red
  MIC LIVE badge plus a tone show when it is on. A panic key stops all sound and mutes the mic.
- A routing check plays a tone and confirms other apps can hear Vocal Ink.

### Streaming and VTubing
- OBS overlays: captions, Twitch chat read aloud, and a built-in PNGtuber. Nine starting looks and full control
  over fonts, colours, position, animations and timing, with a live preview. Changes reach OBS without a reload.
- obs-websocket: add overlays to a scene in one click, text-source subtitles, closed captions, a talking source.
- Twitch chat read aloud in its own voice, with filters; moderator deletes are respected.
- VTube Studio (mouth, smile, expressions, hotkeys), VSeeFace, Warudo, VNyan and VirtualMotionCapture (mouth
  shapes over VMC), veadotube mini, and Streamer.bot actions. Mouth shapes follow the words being said.

### For everyone
- 12 languages: English, Español, Français, Deutsch, Português (Brasil), Italiano, Nederlands, Polski, Türkçe,
  日本語, 한국어, 简体中文. Vocal Ink follows your computer's language; switch any time in Settings.
- Screen readers, full keyboard control, switch access, four themes including high contrast, readable fonts
  (Atkinson Hyperlegible, Lexend, OpenDyslexic), text up to 250%, bigger buttons, reduced motion, tap instead of
  hold, ask before speaking.
- Make it yours: your own ink colour, background tint and texture, card and shadow style, sidebar side, message
  box position, and saved looks you can share as `.vilook` files.
- A setup journey that starts with language and comfort settings and adapts to where you'll talk.
- 30+ rebindable shortcuts that work while games have focus, backups, and an in-app update check.

### Downloads and first start
- **Windows** (installer or portable zip): the files aren't code-signed yet, so SmartScreen may warn you. Choose
  *More info → Run anyway*. If the installer offers *Install the Vocal Ink virtual microphone*, tick it.
  Otherwise use [VB-CABLE](https://vb-audio.com/Cable/) (free): voice output *CABLE Input*, and *CABLE Output*
  as the microphone in other apps. Vocal Ink shows you how.
- **macOS** (Apple Silicon or Intel `.dmg`): the app is signed ad hoc, so the first time right-click it and choose
  *Open*. This release uses [BlackHole 2ch](https://existential.audio/blackhole/) as the virtual microphone
  (`brew install blackhole-2ch`); Vocal Ink's own Mac driver will follow once it has been tested on real Macs.
- **Linux** (`.AppImage`): make it executable and run it. Vocal Ink creates its virtual microphone itself
  (PulseAudio or PipeWire, no password needed).

### Known limitations
- The translations were made with the help of AI and haven't been reviewed by native speakers yet.
  Corrections are very welcome ([how to help](https://github.com/Vocal-Ink/Desktop-app/blob/main/i18n/README.md)).
- Piper has no Japanese or Korean voices yet; use a system voice or a cloud voice for those languages.
- On Linux with Wayland, system-wide shortcuts need XWayland: start Vocal Ink with `QT_QPA_PLATFORM=xcb`.
- The Windows virtual microphone driver needs Microsoft's signature before it can ship in every download
  (see [docs/VIRTUAL_AUDIO.md](https://github.com/Vocal-Ink/Desktop-app/blob/main/docs/VIRTUAL_AUDIO.md)).
