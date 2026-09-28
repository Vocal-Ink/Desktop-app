#pragma once

#include "stt/SttEngine.h"

#include <QObject>
#include <QPointer>
#include <QSet>

class MicCapture;
class Vad;

// Microphone -> (voice activity detection) -> recogniser -> text.
//
// Modes:
//  * PushToTalk: record while startListening() .. stopListening() (button held / hotkey held).
//  * Toggle:     toggleListening() starts, the next call stops and transcribes.
//  * HandsFree:  the mic stays open; each detected utterance is transcribed automatically.
class SttController : public QObject
{
    Q_OBJECT
public:
    enum class Mode { PushToTalk, Toggle, HandsFree };
    Q_ENUM(Mode)

    explicit SttController(QObject *parent = nullptr);
    ~SttController() override;

    void setEngine(SttEngine *engine); // not owned
    SttEngine *engine() const { return m_engine; }

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void setInputDevice(const QByteArray &deviceId); // empty = system default
    void setVadSensitivity(int percent);             // 0..100, higher = picks up quieter speech

    void startListening();   // push-to-talk pressed / hands-free on
    void stopListening();    // push-to-talk released (transcribes what was recorded) / hands-free off
    void toggleListening();
    void cancel();           // stop without transcribing

    bool isListening() const { return m_listening; }
    bool isBusy() const { return !m_pending.isEmpty(); }

    static Mode modeFromString(const QString &s);
    static QString modeToString(Mode m);

    // Recordings shorter than this are ignored (accidental taps); longer ones are
    // sent in pieces of at most MaxRecordingSeconds.
    static constexpr int SampleRate = 16000;
    static constexpr int MinRecordingMs = 300;
    static constexpr int MaxRecordingSeconds = 60;

    // Test hooks: run without a real microphone and feed 16 kHz mono audio directly.
    void setSimulatedInputForTesting(bool simulated) { m_simulatedInput = simulated; }
    void feedForTesting(const QVector<float> &mono16k) { onSamples(mono16k); }

signals:
    void listeningChanged(bool listening);
    void speechActiveChanged(bool active);     // VAD: the user is speaking right now
    void levelChanged(float level);            // 0..1 microphone level for meters
    void busyChanged(bool transcribing);
    void transcript(const QString &text);      // cleaned, non-empty
    void errorOccurred(const QString &message);

private:
    void onSamples(const QVector<float> &mono16k);
    void submit(const QVector<float> &audio);
    void setListening(bool listening);
    void stop(bool transcribe);
    void finishRequest(quint64 requestId);
    void clearPending();
    QString unavailableReason() const; // empty when the engine can take requests

    QPointer<SttEngine> m_engine;
    MicCapture *m_mic = nullptr;
    Vad *m_vad = nullptr;
    Mode m_mode = Mode::PushToTalk;
    bool m_listening = false;
    bool m_simulatedInput = false;
    QVector<float> m_recording;
    quint64 m_nextRequest = 1;
    QSet<quint64> m_pending;
};
