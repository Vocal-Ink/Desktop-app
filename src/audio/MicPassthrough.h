#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

class AudioPlayer;

// Routes the user's real microphone into the main (virtual cable) output, mixed
// with the synthesized voice — for people who can speak a little, or want
// laughter and ambience to come through too.
//
//   Off        never
//   HoldKey    live only while press() .. release() (a held shortcut)
//   ToggleKey  toggle() flips it on/off
//   AlwaysOn   live whenever the app runs
class MicPassthrough : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Off, HoldKey, ToggleKey, AlwaysOn };
    Q_ENUM(Mode)

    static constexpr int SampleRate = 48000;

    explicit MicPassthrough(AudioPlayer *player, QObject *parent = nullptr);
    ~MicPassthrough() override;

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void setInputDevice(const QByteArray &deviceId); // empty = system default input
    void setGainDb(float db);                        // -24 .. +24
    void setGateDb(float thresholdDb);               // noise gate; <= -90 disables it
    // Lower (or mute, with a large value) the real mic while the voice speaks.
    void setDuckDuringSpeech(bool enabled, float duckDb = -18.0f);

    // Shortcut / button handlers.
    void press();   // HoldKey: go live
    void release(); // HoldKey: stop
    void toggle();  // ToggleKey (and a quick mute for AlwaysOn)
    void setLive(bool live); // direct control; ignored in Off mode

    bool isLive() const { return m_live; }

    static Mode modeFromString(const QString &s);
    static QString modeToString(Mode m);

    // The path every captured block takes while live: gain, gate, level meter,
    // then AudioPlayer::writeLive(). Public so tests can feed audio directly.
    void processInput(const QVector<float> &mono, int sampleRate);
    // Tests: go live without opening a real microphone.
    void setDeviceCaptureEnabled(bool enabled);

    // Gain and noise gate (pure DSP, exposed for tests). The gate listens to the
    // input before gain, opens within ~10 ms and closes over ~150 ms after a
    // short hold, with linear ramps so it never clicks. Gain changes glide over
    // a few ms; reset() applies them at once.
    class Processor
    {
    public:
        void setSampleRate(int rate);
        int sampleRate() const { return m_rate; }
        void setGainDb(float db);
        void setGateDb(float thresholdDb);
        void reset();
        void process(float *samples, qsizetype count);
        float gateGain() const { return m_gate; }

    private:
        void updateCoefficients();

        int m_rate = SampleRate;
        float m_gain = 1.0f;       // current, glides towards m_gainTarget
        float m_gainTarget = 1.0f;
        float m_gainGlide = 0.0f;
        bool m_gateEnabled = false;
        float m_openLevel = 0.0f;
        float m_closeLevel = 0.0f;
        float m_env = 0.0f;
        float m_envDecay = 0.0f;
        float m_gate = 1.0f;
        bool m_open = false;
        float m_attackStep = 0.0f;
        float m_releaseStep = 0.0f;
        int m_holdFrames = 0;
        int m_hold = 0;
    };

signals:
    void liveChanged(bool live);
    void levelChanged(float level); // 0..1 input level while live
    void errorOccurred(const QString &message);

private:
    bool startCapture();
    void stopCapture();
    void onCaptureError(const QString &message);

    class Private;
    Private *d;
    AudioPlayer *m_player;
    Mode m_mode = Mode::Off;
    bool m_live = false;
};
