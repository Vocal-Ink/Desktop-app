#pragma once

#include <QVector>

// Streaming sample-rate converter for mono float audio. Keeps state across
// process() calls so audio that arrives in network-sized chunks joins without
// clicks. Uses a windowed-sinc low-pass when downsampling (to avoid aliasing,
// e.g. 48 kHz microphone -> 16 kHz recogniser) and Catmull-Rom interpolation.
class Resampler
{
public:
    Resampler() = default;
    Resampler(int inRate, int outRate) { reset(inRate, outRate); }

    void reset(int inRate, int outRate);
    int inRate() const { return m_inRate; }
    int outRate() const { return m_outRate; }
    bool isPassthrough() const { return m_inRate == m_outRate; }

    QVector<float> process(const float *in, qsizetype count);
    QVector<float> process(const QVector<float> &in) { return process(in.constData(), in.size()); }
    // Emits the samples still held back for interpolation. Call at end of stream.
    QVector<float> flush();

    // One-shot convenience.
    static QVector<float> convert(const QVector<float> &in, int inRate, int outRate);

private:
    void lowpass(const float *in, qsizetype count, QVector<float> &out);

    int m_inRate = 0;
    int m_outRate = 0;
    double m_step = 1.0;  // input samples per output sample
    double m_pos = 0.0;   // read position in m_buf
    QVector<float> m_buf; // filtered input awaiting interpolation
    QVector<float> m_taps;
    QVector<float> m_history; // FIR delay line
};
