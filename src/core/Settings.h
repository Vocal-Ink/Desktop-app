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

// UI
inline constexpr auto Theme = "ui/theme";           // "dark" | "light" | "contrast"
inline constexpr auto FontScale = "ui/fontScale";   // 80..200 (%)
inline constexpr auto FirstRunDone = "ui/firstRunDone";
inline constexpr auto MinimizeToTray = "ui/minimizeToTray";
inline constexpr auto AlwaysOnTop = "ui/alwaysOnTop";
inline constexpr auto ClearAfterSpeak = "ui/clearAfterSpeak";
inline constexpr auto WindowGeometry = "ui/geometry";
inline constexpr auto WindowState = "ui/windowState";
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

    QString string(const char *key) const { return value(key).toString(); }
    int integer(const char *key) const { return value(key).toInt(); }
    bool flag(const char *key) const { return value(key).toBool(); }

    static QVariant defaultValue(const char *key);

    void sync();

signals:
    void changed(const QString &key);

private:
    std::unique_ptr<QSettings> m_settings;
};
