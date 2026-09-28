#include "audio/AudioOutputLane.h"

#include "audio/AudioConvert.h"
#include "audio/Resampler.h"

#include <QAudioSink>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutex>
#include <QTimer>
#include <cstring>
#include <limits>

namespace {

// Pull-mode source: hands queued PCM to the sink and pads with silence so the
// stream never underruns (keeps the virtual cable "alive" between messages).
class PcmSource : public QIODevice
{
public:
    explicit PcmSource(QObject *parent)
        : QIODevice(parent)
    {
    }

    void setSilence(char byte) { m_silence = byte; }

    void append(const QByteArray &bytes)
    {
        QMutexLocker lock(&m_mutex);
        m_buf.append(bytes);
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_buf.clear();
        m_pos = 0;
    }

    qint64 pending() const
    {
        QMutexLocker lock(&m_mutex);
        return m_buf.size() - m_pos;
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return std::numeric_limits<qint32>::max(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        QMutexLocker lock(&m_mutex);
        const qint64 avail = m_buf.size() - m_pos;
        const qint64 n = std::min(avail, maxlen);
        if (n > 0) {
            std::memcpy(data, m_buf.constData() + m_pos, size_t(n));
            m_pos += n;
            if (m_pos == m_buf.size() || m_pos > (1 << 20)) {
                m_buf.remove(0, m_pos);
                m_pos = 0;
            }
        }
        if (n < maxlen)
            std::memset(data + n, m_silence, size_t(maxlen - n));
        return maxlen;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    mutable QMutex m_mutex;
    QByteArray m_buf;
    qint64 m_pos = 0;
    char m_silence = 0;
};

constexpr int kIdleCloseMs = 45000;
constexpr int kPollMs = 20;

} // namespace

class QtAudioLane::Private
{
public:
    QAudioDevice device;
    QAudioFormat format;
    QAudioSink *sink = nullptr;
    PcmSource *source = nullptr;
    Resampler resampler;
    int sourceRate = 0;
    float gain = 1.0f;
    bool finishing = false;
    bool drainCountdown = false;
    QElapsedTimer drainTimer;
    QElapsedTimer idleTimer;
    QTimer *poll = nullptr;
    bool reportedError = false;
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

    d->poll = new QTimer(this);
    d->poll->setInterval(kPollMs);
    connect(d->poll, &QTimer::timeout, this, [this] {
        if (!d->source)
            return;
        if (d->finishing) {
            if (d->source->pending() > 0)
                return;
            if (!d->drainCountdown) {
                d->drainCountdown = true;
                d->drainTimer.start();
            }
            // Wait for the sink's own buffer to play out.
            const qint64 latencyMs = d->sink
                ? d->format.durationForBytes(d->sink->bufferSize()) / 1000 + 30
                : 0;
            if (d->drainTimer.elapsed() >= latencyMs) {
                d->finishing = false;
                d->drainCountdown = false;
                d->idleTimer.start();
                emit drained();
            }
            return;
        }
        if (d->source->pending() == 0 && d->idleTimer.isValid() && d->idleTimer.elapsed() > kIdleCloseMs) {
            // Release the device after a long silence; reopened on the next write.
            if (d->sink)
                d->sink->stop();
            d->idleTimer.invalidate();
            d->poll->stop();
        }
    });
}

QtAudioLane::~QtAudioLane()
{
    if (d->sink)
        d->sink->stop();
    delete d;
}

QString QtAudioLane::deviceName() const
{
    return d->device.description();
}

void QtAudioLane::setSourceRate(int rate)
{
    if (rate == d->sourceRate)
        return;
    if (d->sourceRate > 0 && !d->resampler.isPassthrough()) {
        const QVector<float> tail = d->resampler.flush();
        if (!tail.isEmpty() && d->source)
            d->source->append(AudioConvert::fromMonoFloat(tail.constData(), tail.size(), d->format));
    }
    d->sourceRate = rate;
    d->resampler.reset(rate, d->format.sampleRate());
}

void QtAudioLane::write(const float *samples, qsizetype count)
{
    if (count <= 0)
        return;
    if (!d->sink) {
        d->sink = new QAudioSink(d->device, d->format, this);
        d->sink->setBufferSize(d->format.bytesForDuration(120000)); // ~120 ms
        d->sink->setVolume(d->gain);
        d->source = new PcmSource(this);
        d->source->setSilence(d->format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : char(0));
        d->source->open(QIODevice::ReadOnly);
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
        d->source->clear();
        d->reportedError = false;
        d->sink->start(d->source);
    }
    if (d->sourceRate <= 0)
        setSourceRate(d->format.sampleRate());

    const QVector<float> converted = d->resampler.isPassthrough()
        ? QVector<float>(samples, samples + count)
        : d->resampler.process(samples, count);
    d->source->append(AudioConvert::fromMonoFloat(converted.constData(), converted.size(), d->format));
    d->idleTimer.start();
    if (!d->poll->isActive())
        d->poll->start();
}

void QtAudioLane::finish()
{
    if (!d->resampler.isPassthrough() && d->source) {
        const QVector<float> tail = d->resampler.flush();
        if (!tail.isEmpty())
            d->source->append(AudioConvert::fromMonoFloat(tail.constData(), tail.size(), d->format));
    }
    if (!d->source || !d->sink || d->sink->state() == QAudio::StoppedState) {
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
    if (d->source)
        d->source->clear();
    d->resampler.reset(d->sourceRate, d->format.sampleRate());
    // The sink keeps running (silence) so the next message starts instantly.
}

void QtAudioLane::setGain(float gain)
{
    d->gain = gain;
    if (d->sink)
        d->sink->setVolume(gain);
}
