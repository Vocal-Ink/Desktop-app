#pragma once

#include "audio/AudioOutputLane.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class QTimer;

// Plays synthesized speech to the main output (normally a virtual cable that
// other apps use as a microphone) and, optionally, a monitor output so the
// user hears themselves.
class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    // Tests inject fake lanes; the default creates QtAudioLane.
    void setLaneFactory(AudioLaneFactory factory);

    struct Routing
    {
        QByteArray mainDevice;    // empty = system default output
        float mainGain = 1.0f;
        bool monitorEnabled = false;
        QByteArray monitorDevice; // empty = system default output
        float monitorGain = 0.7f;
    };
    void setRouting(const Routing &routing);
    Routing routing() const { return m_routing; }

    void setSourceRate(int rate);
    void write(const QVector<float> &mono);
    void finish();
    void stop();
    bool isPlaying() const { return m_playing; }
    // How much of the current utterance (write() calls since the last stop() or
    // drained()) has actually been heard on the main output, in milliseconds.
    qint64 playedMs() const;

    // Names of the devices currently in use (for the status bar).
    QStringList activeDeviceNames() const;

    // --- Extra sources mixed with speech --------------------------------------
    // Sound effects (soundboard): mixed into every output, independent of speech.
    // Playing an id that is still playing restarts it. soundFinished(id) is
    // emitted once it ends or is stopped (not when restarted).
    void playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain = 1.0f);
    void stopSound(quint64 id);
    void stopAllSounds();
    bool isSoundPlaying(quint64 id) const;
    // Live microphone passthrough: mixed into the main output only (never the
    // monitor, which would echo the user's own voice back at them).
    void writeLive(const QVector<float> &mono, int sampleRate);
    void clearLive();
    // How much the live microphone is turned down while speech plays (0 = not at all).
    void setLiveDuckingDb(float db);

signals:
    void drained();
    void errorOccurred(const QString &message);
    void soundFinished(quint64 id);
    // Peak (0..1) of what is being heard on the main output (speech, sounds and
    // live input mixed): ~30 Hz while anything is audible, then once 0.0.
    void levelChanged(float peak);
    // Same cadence, but only the synthesized speech (no sounds, no live input):
    // ~30 Hz while the voice is audible, then once 0.0. Drives avatar lip-sync.
    void speechLevelChanged(float peak);

private:
    void rebuildLanes();
    void onLaneDrained();
    void onLaneSoundFinished(quint64 id);
    void startLevelMeter();
    void onLevelTick();

    AudioLaneFactory m_factory;
    Routing m_routing;
    QVector<QPointer<AudioOutputLane>> m_lanes;
    int m_sourceRate = 0;
    int m_waitingDrain = 0;
    bool m_playing = false;
    QHash<quint64, int> m_sounds; // playing sound -> lanes still playing it
    float m_liveDucking = 1.0f;
    qint64 m_utteranceStartUs = 0; // main lane speech timeline at the utterance's first write()
    QTimer *m_levelTimer = nullptr;
    bool m_levelAudible = false;
    bool m_speechAudible = false;
    int m_quietTicks = 0;
    int m_staleTicks = 0;
};
