#include "audio/AudioOutputLane.h"

#include "audio/AudioConvert.h"
#include "audio/AudioMixer.h"

#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace {

// Pull-mode source: renders the mix on demand and never runs dry (silence keeps
// the virtual cable "alive" between messages).
class MixSource : public QIODevice
{
public:
    MixSource(AudioMixer *mixer, const QAudioFormat &format, QObject *parent)
        : QIODevice(parent)
        , m_mixer(mixer)
        , m_format(format)
        , m_silence(format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : char(0))
    {
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return std::numeric_limits<qint32>::max(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int frameBytes = m_format.bytesPerFrame();
        const qint64 frames = frameBytes > 0 ? maxlen / frameBytes : 0;
        if (frames > 0) {
            if (qint64(m_scratch.size()) < frames)
                m_scratch.resize(size_t(frames));
            m_mixer->render(m_scratch.data(), frames);
            AudioConvert::writeMonoFloat(m_scratch.data(), frames, m_format, data);
        }
        const qint64 used = frames * frameBytes;
        if (used < maxlen)
            std::memset(data + used, m_silence, size_t(maxlen - used));
        return maxlen;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    AudioMixer *m_mixer;
    QAudioFormat m_format;
    std::vector<float> m_scratch; // only touched by the pulling thread
    char m_silence;
};

constexpr int kIdleCloseMs = 45000;
constexpr int kPollMs = 20;

} // namespace

class QtAudioLane::Private
{
public:
    QAudioDevice device;
    QAudioFormat format;
    AudioMixer mixer;
    QAudioSink *sink = nullptr;
    MixSource *source = nullptr;
    float gain = 1.0f;
    bool finishing = false;
    bool drainCountdown = false;
    QElapsedTimer drainTimer;
    QElapsedTimer idleTimer;
    QTimer *poll = nullptr;
    bool reportedError = false;
    qint64 peakFrom = 0;
    qint64 speechPeakFrom = 0;

    qint64 toUs(qint64 frames) const { return frames * 1000000 / std::max(1, mixer.outputRate()); }
};

QAudioDevice QtAudioLane::findOutputDevice(const QByteArray &id)
{
    if (!id.isEmpty()) {
        const auto outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &dev : outputs) {
            if (dev.id() == id)
                return dev;
        }
    }
    return QMediaDevices::defaultAudioOutput();
}

QtAudioLane::QtAudioLane(const QByteArray &deviceId, QObject *parent)
    : AudioOutputLane(parent)
    , d(new Private)
{
    d->device = findOutputDevice(deviceId);
    QAudioFormat fmt = d->device.preferredFormat();
    if (!fmt.isValid() || fmt.sampleFormat() == QAudioFormat::Unknown) {
        fmt.setSampleRate(48000);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Int16);
    }
    d->format = fmt;
    d->mixer.setOutputRate(fmt.sampleRate());

    d->poll = new QTimer(this);
    d->poll->setInterval(kPollMs);
    connect(d->poll, &QTimer::timeout, this, &QtAudioLane::onPoll);
}

QtAudioLane::~QtAudioLane()
{
    // Stop pulling before the mixer goes away.
    if (d->sink) {
        d->sink->disconnect(this);
        d->sink->stop();
    }
    delete d->sink;
    delete d->source;
    delete d;
}

QString QtAudioLane::deviceName() const
{
    return d->device.description();
}

void QtAudioLane::ensureOpen()
{
    if (!d->sink) {
        d->sink = new QAudioSink(d->device, d->format, this);
        d->sink->setBufferSize(d->format.bytesForDuration(120000)); // ~120 ms
        d->sink->setVolume(d->gain);
        d->source = new MixSource(&d->mixer, d->format, this);
        // Unbuffered: otherwise QIODevice reads ahead in 16 KB chunks, adding latency.
        d->source->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        connect(d->sink, &QAudioSink::stateChanged, this, [this](QAudio::State) {
            if (d->sink->error() != QAudio::NoError && d->sink->error() != QAudio::UnderrunError
                && !d->reportedError) {
                d->reportedError = true;
                emit errorOccurred(tr("Audio output \"%1\" failed (error %2). Check the device in Settings → Audio.")
                                       .arg(d->device.description())
                                       .arg(int(d->sink->error())));
            }
        });
    }
    if (d->sink->state() == QAudio::StoppedState) {
        d->reportedError = false;
        d->sink->start(d->source);
    }
    d->idleTimer.start();
    if (!d->poll->isActive())
        d->poll->start();
}

void QtAudioLane::onPoll()
{
    const QList<quint64> done = d->mixer.takeFinishedSounds();
    for (quint64 id : done)
        emit soundFinished(id);

    // A dead device never pulls: don't leave speech or sounds waiting forever.
    if (d->sink && d->sink->state() == QAudio::StoppedState && d->sink->error() != QAudio::NoError) {
        d->mixer.clearSpeech();
        d->mixer.clearLive();
        const QList<quint64> dropped = d->mixer.stopAllSounds();
        for (quint64 id : dropped)
            emit soundFinished(id);
        if (d->finishing) {
            d->finishing = false;
            d->drainCountdown = false;
            emit drained();
        }
        d->poll->stop();
        return;
    }

    if (d->finishing) {
        if (d->mixer.pendingSpeech() > 0)
            return;
        if (!d->drainCountdown) {
            d->drainCountdown = true;
            d->drainTimer.start();
        }
        // Wait for the sink's own buffer to play out.
        const qint64 latencyMs = d->sink ? d->format.durationForBytes(qint32(d->sink->bufferSize())) / 1000 + 30 : 0;
        if (d->drainTimer.elapsed() >= latencyMs) {
            d->finishing = false;
            d->drainCountdown = false;
            d->idleTimer.start();
            emit drained();
        }
        return;
    }

    if (!d->mixer.isIdle()) {
        d->idleTimer.start();
        return;
    }
    if (d->idleTimer.isValid() && d->idleTimer.elapsed() > kIdleCloseMs) {
        // Release the device after a long silence; reopened on the next write.
        if (d->sink)
            d->sink->stop();
        d->idleTimer.invalidate();
        d->poll->stop();
    }
}

void QtAudioLane::setSourceRate(int rate)
{
    d->mixer.setSpeechRate(rate);
}

void QtAudioLane::write(const float *samples, qsizetype count)
{
    if (count <= 0)
        return;
    ensureOpen();
    d->mixer.writeSpeech(samples, count);
}

void QtAudioLane::finish()
{
    d->mixer.endSpeech();
    if (!d->sink || d->sink->state() == QAudio::StoppedState) {
        // Nothing is playing: report completion asynchronously.
        QTimer::singleShot(0, this, &AudioOutputLane::drained);
        return;
    }
    d->finishing = true;
    d->drainCountdown = false;
    if (!d->poll->isActive())
        d->poll->start();
}

void QtAudioLane::stop()
{
    d->finishing = false;
    d->drainCountdown = false;
    d->mixer.clearSpeech();
    // The sink keeps running (silence) so the next message starts instantly.
}

void QtAudioLane::setGain(float gain)
{
    d->gain = gain;
    if (d->sink)
        d->sink->setVolume(gain);
}

void QtAudioLane::playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain)
{
    ensureOpen();
    d->mixer.playSound(id, mono, sampleRate, gain);
}

void QtAudioLane::stopSound(quint64 id)
{
    d->mixer.stopSound(id);
}

void QtAudioLane::stopAllSounds()
{
    d->mixer.stopAllSounds();
}

void QtAudioLane::writeLive(const float *samples, qsizetype count, int sampleRate)
{
    if (count <= 0)
        return;
    ensureOpen();
    d->mixer.writeLive(samples, count, sampleRate);
}

void QtAudioLane::clearLive()
{
    d->mixer.clearLive();
}

void QtAudioLane::setLiveDucking(float gain)
{
    d->mixer.setLiveDucking(gain);
}

qint64 QtAudioLane::heardFrame() const
{
    const qint64 buffered = d->sink ? d->format.framesForBytes(qint32(d->sink->bufferSize())) : 0;
    return std::max<qint64>(0, d->mixer.renderedFrames() - buffered);
}

float QtAudioLane::takeOutputPeak()
{
    const qint64 heard = heardFrame();
    if (heard <= d->peakFrom)
        return -1.0f;
    const float peak = d->mixer.peakBetween(d->peakFrom, heard);
    d->peakFrom = std::max(d->peakFrom, heard);
    return peak;
}

float QtAudioLane::takeSpeechPeak()
{
    const qint64 heard = heardFrame();
    if (heard <= d->speechPeakFrom)
        return -1.0f;
    const float peak = d->mixer.speechPeakBetween(d->speechPeakFrom, heard);
    d->speechPeakFrom = std::max(d->speechPeakFrom, heard);
    return peak;
}

qint64 QtAudioLane::speechQueuedUs() const
{
    return d->toUs(d->mixer.speechFramesQueued());
}

qint64 QtAudioLane::speechHeardUs() const
{
    return d->toUs(d->mixer.speechFramesBefore(heardFrame()));
}
