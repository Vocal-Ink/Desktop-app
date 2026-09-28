#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>
#include <memory>

// Setting keys. Grouped by feature; values are plain QVariants in QSettings.
namespace Keys {
// Audio routing
inline constexpr auto OutputDevice = "audio/outputDevice";     // QByteArray device id ("" = system default)
inline constexpr auto OutputVolume = "audio/outputVolume";     // 0..100
inline constexpr auto MonitorEnabled = "audio/monitorEnabled"; // also play to the user's own speakers
inline constexpr auto MonitorDevice = "audio/monitorDevice";
inline constexpr auto MonitorVolume = "audio/monitorVolume";
inline constexpr auto InputDevice = "audio/inputDevice";

// Voice
inline constexpr auto Voice = "tts/voice";                     // Voice::key(), e.g. "piper:en_US-lessac-medium"
inline constexpr auto FavoriteVoices = "tts/favorites";        // QStringList of voice keys
inline constexpr auto Rate = "tts/rate";                       // 50..200 (% of normal)
inline constexpr auto Pitch = "tts/pitch";                     // -50..50
inline constexpr auto SplitSentences = "tts/splitSentences";   // start speaking after the first sentence
inline constexpr auto AzureRegion = "tts/azure/region";
inline constexpr auto ElevenLabsModel = "tts/elevenlabs/model";
inline constexpr auto FishModel = "tts/fish/model";
inline constexpr auto FishCustomVoices = "tts/fish/customVoices";        // QStringList "id|name"
inline constexpr auto ElevenLabsCustomVoices = "tts/elevenlabs/customVoices";
inline constexpr auto OpenAiTtsModel = "tts/openai/model";
inline constexpr auto OpenAiBaseUrl = "tts/openai/baseUrl";
inline constexpr auto OpenAiInstructions = "tts/openai/instructions";
inline constexpr auto PiperExecutable = "tts/piper/executable"; // optional override

// Speech recognition
inline constexpr auto SttEngine = "stt/engine";           // "whisper" | "openai"
inline constexpr auto SttMode = "stt/mode";               // "ptt" | "toggle" | "vad"
inline constexpr auto SttAutoSpeak = "stt/autoSpeak";     // speak immediately vs. review first
inline constexpr auto SttLanguage = "stt/language";       // "auto" or ISO-639-1
inline constexpr auto SttPrompt = "stt/prompt";           // custom vocabulary / names
inline constexpr auto SttVadSensitivity = "stt/vadSensitivity"; // 0..100
inline constexpr auto WhisperModel = "stt/whisper/model"; // file name inside the models dir
inline constexpr auto SttOpenAiModel = "stt/openai/model";
inline constexpr auto SttOpenAiBaseUrl = "stt/openai/baseUrl";

// OBS
inline constexpr auto ObsEnabled = "obs/enabled";
inline constexpr auto ObsHost = "obs/host";
inline constexpr auto ObsPort = "obs/port";
inline constexpr auto ObsSubtitlesEnabled = "obs/subtitles/enabled";
inline constexpr auto ObsSubtitlesSource = "obs/subtitles/source";
inline constexpr auto ObsSubtitlesClearMs = "obs/subtitles/clearAfterMs";
inline constexpr auto ObsCaptionsEnabled = "obs/captions/enabled";
inline constexpr auto ObsIndicatorEnabled = "obs/indicator/enabled";
inline constexpr auto ObsIndicatorSource = "obs/indicator/source";
inline constexpr auto OverlayEnabled = "overlay/enabled";
inline constexpr auto OverlayPort = "overlay/port";
inline constexpr auto OverlayAllowLan = "overlay/allowLan";
inline constexpr auto OverlayQuery = "overlay/query"; // style options appended to the overlay URL

// Hotkeys (QKeySequence portable text)
inline constexpr auto HotkeyPushToTalk = "hotkeys/pushToTalk";
inline constexpr auto HotkeyStop = "hotkeys/stop";
inline constexpr auto HotkeyQuickType = "hotkeys/quickType";
inline constexpr auto HotkeyRepeat = "hotkeys/repeat";

// Text
inline constexpr auto Replacements = "text/replacements"; // QVariantMap abbreviation -> expansion

inline constexpr auto AutoCapitalize = "text/autoCapitalize";
inline constexpr auto EmojiMode = "text/emoji";         // "speak" | "remove" | "keep"
inline constexpr auto UrlMode = "text/urls";            // "say" | "remove" | "keep"
inline constexpr auto Variables = "text/variables";     // QVariantMap {name} -> value
inline constexpr auto Predictions = "text/predictions"; // number of word suggestions (0 = off)
inline constexpr auto Interrupt = "text/interrupt";     // a new message cuts off the current one

// Voice effect
inline constexpr auto Effect = "fx/effect";             // VoiceEffects::id()
inline constexpr auto EffectIntensity = "fx/intensity"; // 0..100

// Real microphone passthrough
inline constexpr auto MicMode = "mic/mode";             // "off" | "hold" | "toggle" | "always"
inline constexpr auto MicDevice = "mic/device";
inline constexpr auto MicGainDb = "mic/gainDb";         // -24..24
inline constexpr auto MicGateDb = "mic/gateDb";         // -90 (off) .. -20
inline constexpr auto MicDuck = "mic/duck";             // lower the mic while the voice speaks
inline constexpr auto MicDuckDb = "mic/duckDb";         // -60..0
inline constexpr auto MicWarnOverlay = "mic/warnOverlay"; // on-screen "mic is live" badge
inline constexpr auto MicWarnSound = "mic/warnSound";

// Twitch chat reader
inline constexpr auto TwitchEnabled = "twitch/enabled";
inline constexpr auto TwitchChannel = "twitch/channel";
inline constexpr auto TwitchVoice = "twitch/voice";     // voice key; empty = a different favorite
inline constexpr auto TwitchReadNames = "twitch/readNames";
inline constexpr auto TwitchSkipCommands = "twitch/skipCommands";
inline constexpr auto TwitchSkipLinks = "twitch/skipLinks";
inline constexpr auto TwitchSubsOnly = "twitch/subsOnly";
inline constexpr auto TwitchIgnored = "twitch/ignoredUsers"; // comma separated
inline constexpr auto TwitchBlocked = "twitch/blockedWords";

// Appearance
inline constexpr auto Theme = "ui/theme";           // "midnight" | "vellum" | "amethyst" | "contrast" | "system"
inline constexpr auto Accent = "ui/accent";         // "#rrggbb"
inline constexpr auto FontScale = "ui/fontScale";   // 80..250 (%)
inline constexpr auto FontFamily = "ui/font";       // "atkinson" | "opendyslexic" | "lexend" | "system"
inline constexpr auto Density = "ui/density";       // "compact" | "comfortable" | "spacious"
inline constexpr auto Corners = "ui/corners";       // "sharp" | "soft" | "round"
inline constexpr auto SidebarLabels = "ui/sidebarLabels";
inline constexpr auto StageScale = "ui/stageScale"; // 60..200 (%) size of the "now speaking" line
inline constexpr auto ComposerSize = "ui/composerSize"; // pt
inline constexpr auto ShowThread = "ui/showThread";
inline constexpr auto ShowPhraseTray = "ui/showPhraseTray";
inline constexpr auto WaveStyle = "ui/waveStyle";   // "ink" | "bars" | "off"
inline constexpr auto InkEffect = "ui/inkEffect";   // words fill with ink as they're spoken
inline constexpr auto Motion = "ui/motion";         // "full" | "reduced" | "off"
inline constexpr auto CompactOpacity = "ui/compactOpacity"; // 40..100 (%)
inline constexpr auto OnboardingDone = "ui/onboardingDone";
inline constexpr auto Uses = "ui/uses";             // QStringList: "calls" "stream" "inperson" "games"
inline constexpr auto FirstRunDone = "ui/firstRunDone";
inline constexpr auto MinimizeToTray = "ui/minimizeToTray";
inline constexpr auto AlwaysOnTop = "ui/alwaysOnTop";
inline constexpr auto ClearAfterSpeak = "ui/clearAfterSpeak";
inline constexpr auto WindowGeometry = "ui/geometry";
inline constexpr auto WindowState = "ui/windowState";
inline constexpr auto CheckUpdates = "app/checkUpdates";

// Accessibility
inline constexpr auto FocusRing = "a11y/focusRing";         // "normal" | "bold"
inline constexpr auto LargeTargets = "a11y/largeTargets";
inline constexpr auto LetterSpacing = "a11y/letterSpacing"; // 0..20 (% of font size)
inline constexpr auto LineSpacing = "a11y/lineSpacing";     // 100..200 (%)
inline constexpr auto SoundCues = "a11y/soundCues";
inline constexpr auto SoundCueVolume = "a11y/soundCueVolume"; // 0..100
inline constexpr auto Announce = "a11y/announce";           // screen reader announcements
inline constexpr auto Scanning = "a11y/scanning";           // switch-access scanning
inline constexpr auto ScanIntervalMs = "a11y/scanIntervalMs";
inline constexpr auto ConfirmSpeak = "a11y/confirmSpeak";   // ask before speaking a message
inline constexpr auto LatchPtt = "a11y/latchPtt";           // tap to start/stop instead of holding
inline constexpr auto EchoTyping = "a11y/echoTyping";       // "off" | "words" | "sentences"
inline constexpr auto DebounceMs = "a11y/debounceMs";       // ignore repeated presses (tremor)
inline constexpr auto HighlightSpoken = "a11y/highlightSpoken"; // follow along word by word
} // namespace Keys

// Thin QSettings wrapper with central defaults and a change signal so services
// can react to edits made in the settings dialog.
class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);
    // For tests: settings stored in an explicit ini file.
    Settings(const QString &iniPath, QObject *parent);

    QVariant value(const char *key) const;
    QVariant value(const char *key, const QVariant &fallback) const;
    void setValue(const char *key, const QVariant &value);
    void remove(const char *key);

    // Dynamic keys (e.g. "keybinds/<action id>") that have no central default.
    QVariant value(const QString &key, const QVariant &fallback) const;
    void setValue(const QString &key, const QVariant &value);
    bool contains(const QString &key) const;
    void remove(const QString &key);
    QStringList childKeys(const QString &group) const;
    QStringList allKeys() const; // everything stored (not the defaults), e.g. for backups

    QString string(const char *key) const { return value(key).toString(); }
    int integer(const char *key) const { return value(key).toInt(); }
    bool flag(const char *key) const { return value(key).toBool(); }

    static QVariant defaultValue(const char *key);
    static QVariant defaultValue(const QString &key);
    static QStringList knownKeys(); // every key that has a central default

    void sync();

signals:
    void changed(const QString &key);

private:
    std::unique_ptr<QSettings> m_settings;
};
