#pragma once

#include "stt/SttEngine.h"
#include "tts/Voice.h"

#include <QObject>
#include <QPointer>

class AudioPlayer;
class GlobalHotkeys;
class HistoryModel;
class ModelManager;
class ObsIntegration;
class OverlayServer;
class PhraseStore;
class QNetworkAccessManager;
class SecretStore;
class Settings;
class SpeechQueue;
class SttController;
class TtsRegistry;

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

    // --- Actions ---
    // Expands abbreviations and queues the text. Returns the message id (0 = nothing to say).
    quint64 speak(const QString &text, const QString &voiceKey = QString());
    void stopSpeaking();
    void skipCurrent();
    void repeatLast();

    Voice currentVoice() const;
    void setCurrentVoice(const Voice &voice);

    // Re-read settings for one area after the settings dialog changed them.
    void applyAudioRouting();
    void applySpeechOptions();
    void applySttSettings();
    void applyObsSettings();
    void applyOverlaySettings();
    void applyHotkeys();
    void applyAll();

    QString overlayUrl() const;

signals:
    // level: 0 = info, 1 = warning, 2 = error
    void notify(const QString &message, int level);
    void currentVoiceChanged(const Voice &voice);
    void transcriptReady(const QString &text); // recognised speech to review before speaking
    void quickTypeRequested();
    void showWindowRequested();

private:
    void wireSpeech();
    void wireStt();
    void onHotkeyPressed(const QString &id);
    void onHotkeyReleased(const QString &id);
    void recreateSttEngine();
    void applySttOptions();
    void resolveVoiceWhenReady();

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
    bool m_voiceResolved = false;
};
