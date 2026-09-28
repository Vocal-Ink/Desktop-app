#include "audio/AudioPlayer.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QTimer>

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
    , m_factory([](const QByteArray &id, QObject *p) -> AudioOutputLane * { return new QtAudioLane(id, p); })
{
}

AudioPlayer::~AudioPlayer() = default;

void AudioPlayer::setLaneFactory(AudioLaneFactory factory)
{
    m_factory = std::move(factory);
    rebuildLanes();
}

void AudioPlayer::setRouting(const Routing &routing)
{
    const bool devicesChanged = routing.mainDevice != m_routing.mainDevice
        || routing.monitorEnabled != m_routing.monitorEnabled
        || routing.monitorDevice != m_routing.monitorDevice || m_lanes.isEmpty();
    m_routing = routing;
    if (devicesChanged) {
        rebuildLanes();
        return;
    }
    if (!m_lanes.isEmpty() && m_lanes.at(0))
        m_lanes.at(0)->setGain(routing.mainGain);
    if (m_lanes.size() > 1 && m_lanes.at(1))
        m_lanes.at(1)->setGain(routing.monitorGain);
}

void AudioPlayer::rebuildLanes()
{
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane) {
            lane->stop();
            lane->deleteLater();
        }
    }
    m_lanes.clear();
    m_waitingDrain = 0;
    const bool wasPlaying = m_playing;
    m_playing = false;

    auto add = [this](const QByteArray &id, float gain) {
        AudioOutputLane *lane = m_factory(id, this);
        lane->setGain(gain);
        if (m_sourceRate > 0)
            lane->setSourceRate(m_sourceRate);
        connect(lane, &AudioOutputLane::drained, this, &AudioPlayer::onLaneDrained);
        connect(lane, &AudioOutputLane::errorOccurred, this, &AudioPlayer::errorOccurred);
        m_lanes.append(lane);
    };
    add(m_routing.mainDevice, m_routing.mainGain);

    // Skip the monitor when it would play to the very same device as the main output.
    if (m_routing.monitorEnabled) {
        const QByteArray mainId = m_routing.mainDevice.isEmpty()
            ? QMediaDevices::defaultAudioOutput().id() : m_routing.mainDevice;
        const QByteArray monId = m_routing.monitorDevice.isEmpty()
            ? QMediaDevices::defaultAudioOutput().id() : m_routing.monitorDevice;
        if (monId != mainId)
            add(m_routing.monitorDevice, m_routing.monitorGain);
    }
    if (wasPlaying)
        emit drained(); // the interrupted utterance is over
}

QStringList AudioPlayer::activeDeviceNames() const
{
    QStringList names;
    for (const auto &lane : m_lanes) {
        if (lane)
            names << lane->deviceName();
    }
    return names;
}

void AudioPlayer::setSourceRate(int rate)
{
    m_sourceRate = rate;
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->setSourceRate(rate);
    }
}

void AudioPlayer::write(const QVector<float> &mono)
{
    if (mono.isEmpty())
        return;
    if (m_lanes.isEmpty())
        rebuildLanes();
    m_playing = true;
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->write(mono.constData(), mono.size());
    }
}

void AudioPlayer::finish()
{
    if (m_lanes.isEmpty())
        rebuildLanes();
    m_playing = true;
    m_waitingDrain = 0;
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane) {
            ++m_waitingDrain;
            lane->finish();
        }
    }
    if (m_waitingDrain == 0) {
        m_playing = false;
        emit drained();
    }
}

void AudioPlayer::stop()
{
    m_waitingDrain = 0;
    m_playing = false;
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->stop();
    }
}

void AudioPlayer::onLaneDrained()
{
    if (m_waitingDrain <= 0)
        return; // stale notification after stop()
    if (--m_waitingDrain == 0) {
        m_playing = false;
        emit drained();
    }
}

// Placeholders until the mixing engine lands (audio work package).
void AudioPlayer::playSound(quint64 id, const QVector<float> &, int, float)
{
    QTimer::singleShot(0, this, [this, id] { emit soundFinished(id); });
}
void AudioPlayer::stopSound(quint64) {}
void AudioPlayer::stopAllSounds() {}
bool AudioPlayer::isSoundPlaying(quint64) const { return false; }
void AudioPlayer::writeLive(const QVector<float> &, int) {}
void AudioPlayer::clearLive() {}
void AudioPlayer::setLiveDuckingDb(float) {}
