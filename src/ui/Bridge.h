#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <memory>
#include <QPointer>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>

class AppContext;
class DeviceModel;
class DownloadModel;
class LanguageManager;
class A11yObserver;
class HistoryModel;
class KeybindModel;
class PhraseModel;
class PrefsMap;
class PresetModel;
class QTimer;
class RoutingCheck;
class SoundModel;
class VirtualDriver;
class VoiceModel;
class VoicePreview;
struct Voice;

// Everything the QML interface sees, exposed as the `App` singleton.
class Bridge : public QObject
{
    Q_OBJECT
    // Settings as a live map: App.prefs["ui/theme"]; writing a key saves it.
    Q_PROPERTY(QObject *prefs READ prefs CONSTANT)

    Q_PROPERTY(QObject *voices READ voices CONSTANT)
    Q_PROPERTY(QObject *phrases READ phrases CONSTANT)
    Q_PROPERTY(QObject *sounds READ sounds CONSTANT)
    Q_PROPERTY(QObject *keybinds READ keybinds CONSTANT)
    Q_PROPERTY(QObject *outputs READ outputs CONSTANT)
    Q_PROPERTY(QObject *inputs READ inputs CONSTANT)
    Q_PROPERTY(QObject *whisperModels READ whisperModels CONSTANT)
    Q_PROPERTY(QObject *piperVoices READ piperVoices CONSTANT)
    Q_PROPERTY(QObject *presets READ presets CONSTANT)
    Q_PROPERTY(QObject *history READ history CONSTANT)
    Q_PROPERTY(QObject *virtualMic READ virtualMic CONSTANT)
    Q_PROPERTY(QObject *avatar READ avatar CONSTANT) // AvatarController (.vts, .vmc, .veado)
    Q_PROPERTY(QObject *language READ language CONSTANT) // LanguageManager

    // Speaking
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(int queued READ queued NOTIFY queuedChanged)
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY currentLineChanged)
    Q_PROPERTY(double currentId READ currentId NOTIFY currentLineChanged)
    Q_PROPERTY(double lineProgress READ lineProgress NOTIFY lineProgressChanged)
    Q_PROPERTY(double outputLevel READ outputLevel NOTIFY outputLevelChanged)
    Q_PROPERTY(bool captionsPaused READ captionsPaused NOTIFY captionsPausedChanged)

    // Current voice
    Q_PROPERTY(QString voiceKey READ voiceKey NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceName READ voiceName NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceProvider READ voiceProvider NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceProviderName READ voiceProviderName NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceLanguage READ voiceLanguage NOTIFY voiceChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY previewingChanged)
    Q_PROPERTY(QString previewKey READ previewKey NOTIFY previewingChanged)

    // Listening (speech to text)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY transcribingChanged)
    Q_PROPERTY(bool voiceActive READ voiceActive NOTIFY voiceActiveChanged)
    Q_PROPERTY(double micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(bool sttReady READ sttReady NOTIFY sttChanged)
    Q_PROPERTY(QString sttStatus READ sttStatus NOTIFY sttChanged)

    // Real microphone passthrough
    Q_PROPERTY(bool micLive READ micLive NOTIFY micLiveChanged)
    Q_PROPERTY(double micLiveLevel READ micLiveLevel NOTIFY micLiveLevelChanged)

    // Routing
    Q_PROPERTY(QString routeState READ routeState NOTIFY routeChanged)   // "virtual" | "speakers" | "missing"
    Q_PROPERTY(QString routeName READ routeName NOTIFY routeChanged)
    Q_PROPERTY(bool routingCheckRunning READ routingCheckRunning NOTIFY routingCheckRunningChanged)

    // Stream
    Q_PROPERTY(int obsStatus READ obsStatus NOTIFY obsChanged) // ObsIntegration::Status
    Q_PROPERTY(QString obsStatusText READ obsStatusText NOTIFY obsChanged)
    Q_PROPERTY(bool overlayRunning READ overlayRunning NOTIFY overlayChanged)
    Q_PROPERTY(QString overlayUrl READ overlayUrl NOTIFY overlayChanged)
    Q_PROPERTY(int overlayClients READ overlayClients NOTIFY overlayChanged)
    Q_PROPERTY(bool twitchConnected READ twitchConnected NOTIFY twitchChanged)
    Q_PROPERTY(QString twitchStatus READ twitchStatus NOTIFY twitchChanged)

    // App
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool hotkeysSupported READ hotkeysSupported CONSTANT)
    // A screen reader or other assistive technology is using the app's
    // accessibility interface (also true for some input tools on Windows).
    Q_PROPERTY(bool assistiveTech READ assistiveTech NOTIFY assistiveTechChanged)
    Q_PROPERTY(QString hotkeysUnsupportedReason READ hotkeysUnsupportedReason NOTIFY translationsChanged)
    Q_PROPERTY(QVariantList effects READ effects NOTIFY translationsChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
    Q_PROPERTY(QString updateUrl READ updateUrl NOTIFY updateChanged)
    Q_PROPERTY(QString updateNotes READ updateNotes NOTIFY updateChanged)
    Q_PROPERTY(int secretsRevision READ secretsRevision NOTIFY secretsChanged)
    Q_PROPERTY(int learnedWords READ learnedWords NOTIFY learnedWordsChanged)
    Q_PROPERTY(QString dataFolder READ dataFolder CONSTANT)
    // Action id -> current shortcut (portable text), including defaults.
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY shortcutsChanged)

public:
    explicit Bridge(AppContext *context, QObject *parent = nullptr);
    ~Bridge() override;

    void setMainWindow(QQuickWindow *window) { m_window = window; }
    // Sample conversation mid-sentence, for screenshots (--demo).
    void startDemo();

    QObject *prefs() const;
    QObject *voices() const;
    QObject *phrases() const;
    QObject *sounds() const;
    QObject *keybinds() const;
    QObject *outputs() const;
    QObject *inputs() const;
    QObject *whisperModels() const;
    QObject *piperVoices() const;
    QObject *presets() const;
    QObject *history() const;
    QObject *virtualMic() const;
    QObject *avatar() const;
    QObject *language() const;
    // Called once from main(); refreshes texts built in C++ when the language changes.
    void setLanguageManager(LanguageManager *languages);

    bool speaking() const { return m_speaking; }
    int queued() const { return m_queued; }
    QString currentLine() const { return m_currentLine; }
    double currentId() const { return double(m_currentId); }
    double lineProgress() const { return m_lineProgress; }
    double outputLevel() const { return m_outputLevel; }
    bool captionsPaused() const;

    QString voiceKey() const;
    QString voiceName() const;
    QString voiceProvider() const;
    QString voiceProviderName() const;
    QString voiceLanguage() const;
    bool previewing() const { return m_previewing; }
    QString previewKey() const { return m_previewKey; }

    bool listening() const;
    bool transcribing() const { return m_transcribing; }
    bool voiceActive() const { return m_voiceActive; }
    double micLevel() const { return m_micLevel; }
    bool sttReady() const;
    QString sttStatus() const;

    bool micLive() const;
    double micLiveLevel() const { return m_micLiveLevel; }

    QString routeState() const;
    QString routeName() const;
    bool routingCheckRunning() const;

    int obsStatus() const;
    QString obsStatusText() const;
    bool overlayRunning() const;
    QString overlayUrl() const;
    int overlayClients() const;
    bool twitchConnected() const;
    QString twitchStatus() const { return m_twitchStatus; }

    QString platform() const;
    QString version() const;
    bool hotkeysSupported() const;
    bool assistiveTech() const;
    QString hotkeysUnsupportedReason() const;
    QVariantList effects() const;
    QString updateVersion() const { return m_updateVersion; }
    QString updateUrl() const { return m_updateUrl; }
    QString updateNotes() const { return m_updateNotes; }
    int secretsRevision() const { return m_secretsRevision; }
    int learnedWords() const;
    QString dataFolder() const;
    QVariantMap shortcuts() const;

    // --- Speaking ---
    Q_INVOKABLE quint64 speak(const QString &text, const QString &voiceKey = QString());
    Q_INVOKABLE QString prepare(const QString &text) const;
    Q_INVOKABLE void stop();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void repeatLast();
    Q_INVOKABLE void panic();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QStringList recentTexts() const; // newest first, for Up/Down recall
    Q_INVOKABLE QStringList suggest(const QString &textBeforeCursor) const;
    Q_INVOKABLE QString applySuggestion(const QString &textBeforeCursor, const QString &word) const;
    Q_INVOKABLE void forgetLearnedWords();
    // Speaks to the user's own speakers only (typing echo, previews).
    Q_INVOKABLE void echo(const QString &text);

    // --- Actions & shortcuts ---
    Q_INVOKABLE void trigger(const QString &actionId);
    Q_INVOKABLE void triggerHold(const QString &actionId, bool pressed);
    Q_INVOKABLE void suspendHotkeys(bool suspended);
    // Portable text for a key press, or "" while only modifiers are held.
    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;
    Q_INVOKABLE QString nativeShortcut(const QString &portable) const;
    Q_INVOKABLE QStringList shortcutParts(const QString &portable) const; // ["Ctrl", "Alt", "S"]
    Q_INVOKABLE QString shortcutFor(const QString &actionId) const;       // native text, "" if unbound
    Q_INVOKABLE QVariantList commands() const; // everything the command palette can run

    // --- Voices ---
    Q_INVOKABLE void setVoice(const QString &key);
    // {key, name, provider, providerName, language, local, initials, valid}
    Q_INVOKABLE QVariantMap voiceInfo(const QString &key) const;
    Q_INVOKABLE void previewVoice(const QString &key, const QString &text = QString());
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void applyPreset(const QString &presetId);
    Q_INVOKABLE QString saveCurrentAsPreset(const QString &name);
    Q_INVOKABLE void refreshVoices();
    Q_INVOKABLE void searchProvider(const QString &providerId, const QString &query);
    Q_INVOKABLE void addCustomVoice(const QString &providerId, const QString &voiceId, const QString &name);
    Q_INVOKABLE void testOutput();

    // --- Listening ---
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void stopListening();
    Q_INVOKABLE void toggleListening();
    Q_INVOKABLE void cancelListening();

    // --- Real mic ---
    Q_INVOKABLE void setMicLive(bool live);

    // --- Routing ---
    Q_INVOKABLE void startRoutingCheck();
    Q_INVOKABLE void cancelRoutingCheck();
    Q_INVOKABLE void useDevice(const QString &which, const QString &hexId); // "output" | "monitor" | "input" | "mic"
    Q_INVOKABLE QString suggestedOutput() const; // hex id of a virtual cable, or ""

    // --- Stream ---
    Q_INVOKABLE void obsReconnect();
    Q_INVOKABLE void obsTest();
    Q_INVOKABLE void obsAddOverlay();
    Q_INVOKABLE void obsFetchSources(const QString &kind); // "text" | "all"
    Q_INVOKABLE void obsCreateTextSource(const QString &name);
    Q_INVOKABLE QString overlayUrlFor(const QString &query) const;
    Q_INVOKABLE void clearCaptions();
    Q_INVOKABLE void setCaptionsPaused(bool paused);

    // --- Keys, files, misc ---
    Q_INVOKABLE bool hasSecret(const QString &name) const;
    Q_INVOKABLE QString secretHint(const QString &name) const; // "••••1a2b"
    Q_INVOKABLE void setSecret(const QString &name, const QString &value);
    Q_INVOKABLE QString exportSettings(const QUrl &file);
    Q_INVOKABLE QString importSettings(const QUrl &file);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void openDataFolder();
    Q_INVOKABLE void announce(const QString &text, bool assertive = false);
    Q_INVOKABLE void notifyUser(const QString &message, int level = 0);
    Q_INVOKABLE void quit();
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE QUrl fileUrl(const QString &path) const;

signals:
    void translationsChanged();
    void assistiveTechChanged();
    void speakingChanged();
    void queuedChanged();
    void currentLineChanged();
    void lineProgressChanged();
    void outputLevelChanged();
    void captionsPausedChanged();
    void voiceChanged();
    void previewingChanged();
    void listeningChanged();
    void transcribingChanged();
    void voiceActiveChanged();
    void micLevelChanged();
    void sttChanged();
    void micLiveChanged();
    void micLiveLevelChanged();
    void routeChanged();
    void routingCheckRunningChanged();
    void obsChanged();
    void overlayChanged();
    void twitchChanged();
    void updateChanged();
    void secretsChanged();
    void learnedWordsChanged();
    void shortcutsChanged();

    void notify(const QString &message, int level);
    void transcriptReady(const QString &text);
    void uiAction(const QString &actionId);
    // Real mic went live/muted; fromShortcut = a keybind did it (warn loudly).
    void micLiveWarning(bool live, bool fromShortcut);
    void hotkeysFailed(const QStringList &descriptions);
    void routingProgress(double level);
    void routingFinished(bool heard, const QString &detail);
    void obsSources(const QString &kind, const QStringList &names, const QString &error);
    void obsResult(bool ok, const QString &message);
    void spoken(const QString &text); // a message finished playing completely

private:
    void setCurrentLine(quint64 id, const QString &text);
    void setLineProgress(double progress);
    Voice voice() const;

    AppContext *m_ctx;
    PrefsMap *m_prefs;
    VoiceModel *m_voices;
    PhraseModel *m_phrases;
    SoundModel *m_sounds;
    KeybindModel *m_keybinds;
    DeviceModel *m_outputs;
    DeviceModel *m_inputs;
    DownloadModel *m_whisper;
    DownloadModel *m_piper;
    LanguageManager *m_languages = nullptr;
    std::unique_ptr<A11yObserver> m_a11yObserver;
    PresetModel *m_presetModel;
    VoicePreview *m_preview;
    VoicePreview *m_echo;
    RoutingCheck *m_routing;
    QPointer<QQuickWindow> m_window;
    QTimer *m_progressTimer;
    QElapsedTimer m_lineClock;

    bool m_speaking = false;
    int m_queued = 0;
    quint64 m_currentId = 0;
    QString m_currentLine;
    double m_lineProgress = 0.0;
    bool m_haveRealProgress = false;
    double m_outputLevel = 0.0;
    bool m_previewing = false;
    QString m_previewKey;
    bool m_transcribing = false;
    bool m_voiceActive = false;
    double m_micLevel = 0.0;
    double m_micLiveLevel = 0.0;
    QString m_twitchStatus;
    QString m_updateVersion, m_updateUrl, m_updateNotes;
    int m_secretsRevision = 0;
};
