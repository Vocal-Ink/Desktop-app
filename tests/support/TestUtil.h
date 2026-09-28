#pragma once

#include "audio/AudioConvert.h"
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

// Output lane that records everything and "plays" instantly.
class FakeLane : public AudioOutputLane
{
public:
    explicit FakeLane(QObject *parent = nullptr)
        : AudioOutputLane(parent)
    {
    }
    void setSourceRate(int rate) override { this->rate = rate; }
    void write(const float *s, qsizetype n) override { AudioConvert::append(samples, s, n); }
    void finish() override
    {
        ++finishCount;
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
    }
    void setGain(float g) override { gain = g; }
    QString deviceName() const override { return QStringLiteral("fake"); }

    QVector<float> samples;
    int rate = 0;
    float gain = 1.0f;
    int finishCount = 0;
    int stopCount = 0;
    int drainDelayMs = 5;
    int generation = 0;
};

} // namespace TestUtil
