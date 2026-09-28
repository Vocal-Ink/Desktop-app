#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <functional>

// One playback destination (e.g. the virtual cable, or the user's headphones).
// Receives mono float samples at a source rate and converts them to whatever
// the device wants. Stays open between utterances to avoid start-up latency and
// closes itself after a period of silence.
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

signals:
    void drained();
    void errorOccurred(const QString &message);
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

    static QAudioDevice findOutputDevice(const QByteArray &id);

private:
    class Private;
    Private *d;
};
