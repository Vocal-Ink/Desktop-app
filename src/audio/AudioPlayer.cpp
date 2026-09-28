#include "audio/AudioPlayer.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kLevelIntervalMs = 33;
constexpr float kAudiblePeak = 0.001f; // -60 dBFS
constexpr int kLevelIdleTicks = 30;    // stop polling after ~1 s of silence
constexpr int kLevelStaleTicks = 5;    // no pulls for this long counts as silence
} // namespace

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
    , m_factory([](const QByteArray &id, QObject *p) -> AudioOutputLane * { return new QtAudioLane(id, p); })
    , m_levelTimer(new QTimer(this))
{
    m_levelTimer->setInterval(kLevelIntervalMs);
    connect(m_levelTimer, &QTimer::timeout, this, &AudioPlayer::onLevelTick);
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
    const QList<quint64> interruptedSounds = m_sounds.keys();
    m_sounds.clear();

    auto add = [this](const QByteArray &id, float gain) {
        AudioOutputLane *lane = m_factory(id, this);
        lane->setGain(gain);
        lane->setLiveDucking(m_liveDucking);
        if (m_sourceRate > 0)
            lane->setSourceRate(m_sourceRate);
        connect(lane, &AudioOutputLane::drained, this, &AudioPlayer::onLaneDrained);
        connect(lane, &AudioOutputLane::errorOccurred, this, &AudioPlayer::errorOccurred);
        connect(lane, &AudioOutputLane::soundFinished, this, &AudioPlayer::onLaneSoundFinished);
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
    for (quint64 id : interruptedSounds)
        emit soundFinished(id);
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
    if (!m_playing && m_lanes.at(0))
        m_utteranceStartUs = m_lanes.at(0)->speechQueuedUs();
    m_playing = true;
    startLevelMeter();
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

qint64 AudioPlayer::playedMs() const
{
    if (m_lanes.isEmpty() || !m_lanes.at(0))
        return 0;
    return std::max<qint64>(0, m_lanes.at(0)->speechHeardUs() - m_utteranceStartUs) / 1000;
}

void AudioPlayer::startLevelMeter()
{
    m_quietTicks = 0;
    if (!m_levelTimer->isActive())
        m_levelTimer->start();
}

void AudioPlayer::onLevelTick()
{
    AudioOutputLane *main = m_lanes.isEmpty() ? nullptr : m_lanes.at(0).data();
    float peak = main ? std::min(1.0f, main->takeOutputPeak()) : 0.0f;
    if (peak < 0.0f) {
        // Nothing new reached the speakers since the last tick (devices pull in bursts).
        if (++m_staleTicks < kLevelStaleTicks)
            return;
        peak = 0.0f;
    } else {
        m_staleTicks = 0;
    }
    if (peak >= kAudiblePeak) {
        m_quietTicks = 0;
        m_levelAudible = true;
        emit levelChanged(peak);
        return;
    }
    if (m_levelAudible) {
        m_levelAudible = false;
        emit levelChanged(0.0f);
    }
    if (++m_quietTicks >= kLevelIdleTicks)
        m_levelTimer->stop();
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

void AudioPlayer::playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain)
{
    if (m_lanes.isEmpty())
        rebuildLanes();
    int lanes = 0;
    if (!mono.isEmpty() && sampleRate > 0) {
        for (const auto &lane : std::as_const(m_lanes)) {
            if (lane) {
                lane->playSound(id, mono, sampleRate, gain);
                ++lanes;
            }
        }
    }
    if (lanes == 0) {
        // Nothing to play: still report the end, but not from inside this call.
        m_sounds.remove(id);
        QTimer::singleShot(0, this, [this, id] {
            if (!m_sounds.contains(id))
                emit soundFinished(id);
        });
        return;
    }
    m_sounds.insert(id, lanes);
    startLevelMeter();
}

void AudioPlayer::stopSound(quint64 id)
{
    if (!m_sounds.remove(id))
        return;
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->stopSound(id);
    }
    emit soundFinished(id);
}

void AudioPlayer::stopAllSounds()
{
    const QList<quint64> ids = m_sounds.keys();
    m_sounds.clear();
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->stopAllSounds();
    }
    for (quint64 id : ids)
        emit soundFinished(id);
}

bool AudioPlayer::isSoundPlaying(quint64 id) const
{
    return m_sounds.contains(id);
}

void AudioPlayer::onLaneSoundFinished(quint64 id)
{
    auto it = m_sounds.find(id);
    if (it == m_sounds.end())
        return; // stopped already
    if (--it.value() > 0)
        return; // another output is still playing it
    m_sounds.erase(it);
    emit soundFinished(id);
}

void AudioPlayer::writeLive(const QVector<float> &mono, int sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0)
        return;
    if (m_lanes.isEmpty())
        rebuildLanes();
    // Main output only: the monitor would echo the user's own voice back at them.
    if (!m_lanes.isEmpty() && m_lanes.at(0)) {
        m_lanes.at(0)->writeLive(mono.constData(), mono.size(), sampleRate);
        startLevelMeter();
    }
}

void AudioPlayer::clearLive()
{
    if (!m_lanes.isEmpty() && m_lanes.at(0))
        m_lanes.at(0)->clearLive();
}

void AudioPlayer::setLiveDuckingDb(float db)
{
    // Either sign means "down by"; very large values mute.
    const float down = std::fabs(db);
    m_liveDucking = down >= 80.0f ? 0.0f : std::pow(10.0f, -down / 20.0f);
    for (const auto &lane : std::as_const(m_lanes)) {
        if (lane)
            lane->setLiveDucking(m_liveDucking);
    }
}
