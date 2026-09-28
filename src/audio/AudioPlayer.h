#pragma once

#include "audio/AudioOutputLane.h"

#include <QObject>
#include <QPointer>
#include <QVector>

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

    // Names of the devices currently in use (for the status bar).
    QStringList activeDeviceNames() const;

signals:
    void drained();
    void errorOccurred(const QString &message);

private:
    void rebuildLanes();
    void onLaneDrained();

    AudioLaneFactory m_factory;
    Routing m_routing;
    QVector<QPointer<AudioOutputLane>> m_lanes;
    int m_sourceRate = 0;
    int m_waitingDrain = 0;
    bool m_playing = false;
};
