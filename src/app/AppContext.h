#pragma once

#include "stt/SttEngine.h"
#include "tts/Voice.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>

class ActionRegistry;
class AudioPlayer;
class Earcons;
class GlobalHotkeys;
class HistoryModel;
class MicPassthrough;
class ModelManager;
class ObsIntegration;
class OverlayServer;
class PhraseStore;
class QNetworkAccessManager;
class QTimer;
class SecretStore;
class Settings;
class Soundboard;
class SpeechQueue;
class SttController;
class TtsRegistry;
class TwitchChat;
class UpdateChecker;
class VirtualDriver;
class VoicePresets;
class WordPredictor;

// Owns every service and wires them together. The UI talks to this object;
// tests can construct the individual services directly.
class AppContext : public QObject
{
    Q_OBJECT
public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    // Loads secrets/phrases, creates engines, starts the overlay server, connects
    // to OBS and registers hotkeys (as enabled in the settings).
    void initialize();

    Settings *settings() const { return m_settings; }
    SecretStore *secrets() const { return m_secrets; }
    QNetworkAccessManager *network() const { return m_network; }
    TtsRegistry *tts() const { return m_tts; }
    AudioPlayer *player() const { return m_player; }
    SpeechQueue *speech() const { return m_speech; }
    HistoryModel *history() const { return m_history; }
    PhraseStore *phrases() const { return m_phrases; }
    SttController *stt() const { return m_stt; }
    SttEngine *sttEngine() const { return m_sttEngine; }
    ModelManager *models() const { return m_models; }
    ObsIntegration *obs() const { return m_obs; }
    OverlayServer *overlay() const { return m_overlay; }
    GlobalHotkeys *hotkeys() const { return m_hotkeys; }
    ActionRegistry *actions() const { return m_actions; }
    MicPassthrough *mic() const { return m_mic; }
    Soundboard *soundboard() const { return m_soundboard; }
    Earcons *earcons() const { return m_earcons; }
    WordPredictor *predictor() const { return m_predictor; }
    VoicePresets *presets() const { return m_presets; }
    UpdateChecker *updates() const { return m_updates; }
    TwitchChat *twitch() const { return m_twitch; }
    VirtualDriver *virtualDriver() const { return m_driver; }

    // --- Actions ---
    // Runs the text pipeline (variables, abbreviations, emoji, links,
    // capitals) and queues the result. Returns the message id (0 = nothing to say).
    quint64 speak(const QString &text, const QString &voiceKey = QString());
    // The text exactly as it would be spoken.
    QString prepareText(const QString &text) const;
    void stopSpeaking();
    void skipCurrent();
    void repeatLast();
    // Stops everything that can make sound: speech, soundboard, real mic, listening.
    void panic();

    // Runs a keybindable action (see ActionRegistry). `pressed` is false for
    // the release half of hold actions. Actions that belong to the UI
    // (windows, the command palette...) are forwarded via uiActionRequested().
    void triggerAction(const QString &id, bool pressed = true);

    Voice currentVoice() const;
    void setCurrentVoice(const Voice &voice);
    void cycleVoice(int delta); // through favourites (or every usable voice)
    void applyPreset(const QString &presetId);

    bool captionsPaused() const { return m_captionsPaused; }
    void setCaptionsPaused(bool paused);

    // Re-read settings for one area. Called automatically when settings change.
    void applyAudioRouting();
    void applySpeechOptions();
    void applySttSettings();
    void applyObsSettings();
    void applyOverlaySettings();
    void applyHotkeys();
    void applyMicSettings();
    void applyCueSettings();
    void applyTwitchSettings();
    void applyAll();

    // Temporarily releases every global shortcut (while the user records a new one).
    void setHotkeysSuspended(bool suspended);
    bool hotkeysSuspended() const { return m_hotkeysSuspended; }

    QString overlayUrl() const;

signals:
    // level: 0 = info, 1 = warning, 2 = error
    void notify(const QString &message, int level);
    void currentVoiceChanged(const Voice &voice);
    void transcriptReady(const QString &text); // recognised speech to review before speaking
    void quickTypeRequested();
    void showWindowRequested();
    void uiActionRequested(const QString &actionId);
    // The real microphone went live or muted. `fromShortcut` is true when a
    // keybind (not the on-screen switch) did it, so the UI must warn loudly.
    void micLiveChanged(bool live, bool fromShortcut);
    void captionsPausedChanged(bool paused);
    void hotkeysFailed(const QStringList &descriptions);

private:
    void wireSpeech();
    void wireStt();
    void wireExtras();
    void onHotkeyPressed(const QString &id);
    void onHotkeyReleased(const QString &id);
    void onSettingChanged(const QString &key);
    void recreateSttEngine();
    void applySttOptions();
    void resolveVoiceWhenReady();
    bool debounced(const QString &id);
    void notifyThrottled(const QString &topic, const QString &message, int level);
    void adjustSetting(const char *key, int delta, int min, int max, const QString &label);

    Settings *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    TtsRegistry *m_tts = nullptr;
    AudioPlayer *m_player = nullptr;
    SpeechQueue *m_speech = nullptr;
    HistoryModel *m_history = nullptr;
    PhraseStore *m_phrases = nullptr;
    SttController *m_stt = nullptr;
    QPointer<SttEngine> m_sttEngine;
    ModelManager *m_models = nullptr;
    ObsIntegration *m_obs = nullptr;
    OverlayServer *m_overlay = nullptr;
    GlobalHotkeys *m_hotkeys = nullptr;
    ActionRegistry *m_actions = nullptr;
    MicPassthrough *m_mic = nullptr;
    Soundboard *m_soundboard = nullptr;
    Earcons *m_earcons = nullptr;
    WordPredictor *m_predictor = nullptr;
    VoicePresets *m_presets = nullptr;
    UpdateChecker *m_updates = nullptr;
    TwitchChat *m_twitch = nullptr;
    VirtualDriver *m_driver = nullptr;

    QTimer *m_applyTimer = nullptr;
    QSet<QString> m_pendingAreas;
    QHash<QString, qint64> m_lastPress;
    QHash<QString, qint64> m_lastNotice;
    QElapsedTimer m_clock;
    bool m_voiceResolved = false;
    bool m_initialized = false;
    bool m_hotkeysSuspended = false;
    bool m_captionsPaused = false;
    bool m_micFromShortcut = false;
    bool m_pttLatched = false;
};
