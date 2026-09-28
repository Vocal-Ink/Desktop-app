#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QVector>
#include <functional>

// One playback destination (e.g. the virtual cable, or the user's headphones).
// Receives mono float samples at a source rate and converts them to whatever
// the device wants. Speech, sound effects and live input are mixed together.
// Stays open between utterances to avoid start-up latency and closes itself
// after a period of silence.
class AudioOutputLane : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~AudioOutputLane() override = default;

    virtual void setSourceRate(int rate) = 0;
    virtual void write(const float *samples, qsizetype count) = 0;
    // No more data for this utterance: emits drained() once everything written has played.
    virtual void finish() = 0;
    // Drops buffered audio immediately. No drained() is emitted for it.
    virtual void stop() = 0;
    virtual void setGain(float gain) = 0;
    virtual QString deviceName() const = 0;

    // Sound effects, mixed on top of speech. Playing an id again restarts it;
    // soundFinished() follows once a sound has played to the end (not after stopSound()).
    virtual void playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain) = 0;
    virtual void stopSound(quint64 id) = 0;
    virtual void stopAllSounds() = 0;
    // Live input (microphone passthrough), kept to a short buffer for low latency.
    virtual void writeLive(const float *samples, qsizetype count, int sampleRate) = 0;
    virtual void clearLive() = 0;
    // Gain applied to live input while speech plays (1 = unchanged).
    virtual void setLiveDucking(float gain) = 0;

    // What is actually heard (what the device pulled, minus what still waits in
    // its buffer): the output peak since the last call (0..1, or -1 when nothing
    // new has been heard), and the speech timeline in microseconds — written so
    // far (less anything stop() dropped) and heard so far.
    virtual float takeOutputPeak() = 0;
    virtual qint64 speechQueuedUs() const = 0;
    virtual qint64 speechHeardUs() const = 0;
    // Like takeOutputPeak(), but only the synthesized speech (no sounds, no live
    // input). -1 when nothing new has been heard or the lane can't tell.
    virtual float takeSpeechPeak() { return -1.0f; }

signals:
    void drained();
    void errorOccurred(const QString &message);
    void soundFinished(quint64 id);
};

using AudioLaneFactory = std::function<AudioOutputLane *(const QByteArray &deviceId, QObject *parent)>;

// Real implementation backed by QAudioSink (pull mode).
class QtAudioLane : public AudioOutputLane
{
    Q_OBJECT
public:
    explicit QtAudioLane(const QByteArray &deviceId, QObject *parent = nullptr);
    ~QtAudioLane() override;

    void setSourceRate(int rate) override;
    void write(const float *samples, qsizetype count) override;
    void finish() override;
    void stop() override;
    void setGain(float gain) override;
    QString deviceName() const override;
    void playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain) override;
    void stopSound(quint64 id) override;
    void stopAllSounds() override;
    void writeLive(const float *samples, qsizetype count, int sampleRate) override;
    void clearLive() override;
    void setLiveDucking(float gain) override;
    float takeOutputPeak() override;
    float takeSpeechPeak() override;
    qint64 speechQueuedUs() const override;
    qint64 speechHeardUs() const override;

    static QAudioDevice findOutputDevice(const QByteArray &id);

private:
    void ensureOpen();
    void onPoll();
    qint64 heardFrame() const;

    class Private;
    Private *d;
};
