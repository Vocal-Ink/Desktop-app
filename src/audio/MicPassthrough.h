#pragma once

#include <QByteArray>
#include <QObject>

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
    void setLive(bool live);

    bool isLive() const { return m_live; }

    static Mode modeFromString(const QString &s);
    static QString modeToString(Mode m);

signals:
    void liveChanged(bool live);
    void levelChanged(float level); // 0..1 input level while live
    void errorOccurred(const QString &message);

private:
    class Private;
    Private *d;
    AudioPlayer *m_player;
    Mode m_mode = Mode::Off;
    bool m_live = false;
};
