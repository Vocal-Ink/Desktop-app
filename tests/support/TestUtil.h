#pragma once

#include "audio/AudioConvert.h"
#include "audio/AudioMixer.h"
#include "audio/AudioOutputLane.h"

#include <QTimer>
#include <QVector>
#include <cmath>

namespace TestUtil {

// A sine wave, handy for resampler and VAD tests.
inline QVector<float> sine(double freq, int rate, double seconds, float amplitude = 0.5f)
{
    QVector<float> out(qsizetype(rate * seconds));
    for (qsizetype i = 0; i < out.size(); ++i)
        out[i] = amplitude * float(std::sin(2.0 * 3.14159265358979323846 * freq * double(i) / rate));
    return out;
}

// Estimates the dominant frequency by counting zero crossings.
inline double zeroCrossingFrequency(const QVector<float> &s, int rate)
{
    int crossings = 0;
    for (qsizetype i = 1; i < s.size(); ++i) {
        if ((s[i - 1] < 0.0f) != (s[i] < 0.0f))
            ++crossings;
    }
    return crossings / 2.0 / (double(s.size()) / rate);
}

// Output lane that records everything and "plays" instantly. The real mixer
// sits behind it: pull() renders what a device would hear.
class FakeLane : public AudioOutputLane
{
public:
    explicit FakeLane(QObject *parent = nullptr, int outputRate = 48000)
        : AudioOutputLane(parent)
        , mixer(outputRate)
    {
    }
    void setSourceRate(int rate) override
    {
        this->rate = rate;
        mixer.setSpeechRate(rate);
    }
    void write(const float *s, qsizetype n) override
    {
        AudioConvert::append(samples, s, n);
        mixer.writeSpeech(s, n);
    }
    void finish() override
    {
        ++finishCount;
        mixer.endSpeech();
        const int gen = generation;
        QTimer::singleShot(drainDelayMs, this, [this, gen] {
            if (gen == generation) // not interrupted by stop()
                emit drained();
        });
    }
    void stop() override
    {
        ++stopCount;
        ++generation;
        mixer.clearSpeech();
    }
    void setGain(float g) override { gain = g; }
    QString deviceName() const override { return QStringLiteral("fake"); }

    void playSound(quint64 id, const QVector<float> &mono, int sampleRate, float soundGain) override
    {
        soundsPlayed << id;
        mixer.playSound(id, mono, sampleRate, soundGain);
    }
    void stopSound(quint64 id) override { mixer.stopSound(id); }
    void stopAllSounds() override { mixer.stopAllSounds(); }
    void writeLive(const float *s, qsizetype n, int sampleRate) override
    {
        liveFrames += n;
        mixer.writeLive(s, n, sampleRate);
    }
    void clearLive() override { mixer.clearLive(); }
    void setLiveDucking(float g) override { ducking = g; mixer.setLiveDucking(g); }
    float takeOutputPeak() override
    {
        const qint64 heard = mixer.renderedFrames();
        if (heard <= peakFrom)
            return -1.0f;
        const float p = mixer.peakBetween(peakFrom, heard);
        peakFrom = heard;
        return p;
    }
    qint64 speechQueuedUs() const override { return mixer.speechFramesQueued() * 1000000 / mixer.outputRate(); }
    qint64 speechHeardUs() const override
    {
        return mixer.speechFramesBefore(mixer.renderedFrames()) * 1000000 / mixer.outputRate();
    }

    // Renders `frames` of output like a device callback, then reports finished sounds.
    QVector<float> pull(qsizetype frames)
    {
        QVector<float> out(frames);
        mixer.render(out.data(), frames);
        const QList<quint64> done = mixer.takeFinishedSounds();
        for (quint64 id : done)
            emit soundFinished(id);
        return out;
    }

    AudioMixer mixer;
    QVector<float> samples;
    int rate = 0;
    float gain = 1.0f;
    float ducking = 1.0f;
    int finishCount = 0;
    int stopCount = 0;
    int drainDelayMs = 5;
    int generation = 0;
    QList<quint64> soundsPlayed;
    qsizetype liveFrames = 0;
    qint64 peakFrom = 0;
};

} // namespace TestUtil
