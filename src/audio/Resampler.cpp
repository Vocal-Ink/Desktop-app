#include "audio/Resampler.h"

#include "audio/AudioConvert.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kTaps = 33; // odd, linear phase

inline float catmullRom(float y0, float y1, float y2, float y3, float t)
{
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}
} // namespace

void Resampler::reset(int inRate, int outRate)
{
    m_inRate = inRate;
    m_outRate = outRate;
    m_step = (inRate > 0 && outRate > 0) ? double(inRate) / double(outRate) : 1.0;
    m_pos = 0.0;
    m_buf.clear();
    m_taps.clear();
    m_history.clear();

    if (outRate > 0 && outRate < inRate) {
        // Windowed-sinc low-pass at 90% of the output Nyquist frequency.
        const double fc = 0.45 * double(outRate) / double(inRate); // cycles per input sample
        m_taps.resize(kTaps);
        const int m = kTaps - 1;
        double sum = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            const double x = i - m / 2.0;
            const double sinc = x == 0.0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / m) + 0.08 * std::cos(4.0 * kPi * i / m);
            m_taps[i] = float(sinc * w);
            sum += sinc * w;
        }
        for (float &t : m_taps)
            t = float(t / sum);
        m_history.fill(0.0f, kTaps - 1);
    }
}

void Resampler::lowpass(const float *in, qsizetype count, QVector<float> &out)
{
    if (m_taps.isEmpty()) {
        AudioConvert::append(out, in, count);
        return;
    }
    // Delay line = history (kTaps-1) followed by new input.
    QVector<float> line;
    line.reserve(m_history.size() + count);
    line += m_history;
    AudioConvert::append(line, in, count);
    const qsizetype base = out.size();
    out.resize(base + count);
    for (qsizetype i = 0; i < count; ++i) {
        const float *x = line.constData() + i;
        float acc = 0.0f;
        for (int k = 0; k < kTaps; ++k)
            acc += m_taps[k] * x[kTaps - 1 - k];
        out[base + i] = acc;
    }
    m_history = line.mid(line.size() - (kTaps - 1));
}

QVector<float> Resampler::process(const float *in, qsizetype count)
{
    if (count <= 0)
        return {};
    if (m_inRate <= 0 || m_outRate <= 0 || m_inRate == m_outRate)
        return QVector<float>(in, in + count);

    lowpass(in, count, m_buf);

    QVector<float> out;
    out.reserve(qsizetype(double(count) / m_step) + 4);
    const qsizetype n = m_buf.size();
    const float *b = m_buf.constData();
    while (true) {
        const qsizetype i = qsizetype(std::floor(m_pos));
        if (i + 2 >= n)
            break;
        const float t = float(m_pos - double(i));
        const float y0 = i > 0 ? b[i - 1] : b[i];
        out.push_back(catmullRom(y0, b[i], b[i + 1], b[i + 2], t));
        m_pos += m_step;
    }
    // Drop consumed input, keeping one sample of look-behind.
    const qsizetype consumed = qsizetype(std::floor(m_pos)) - 1;
    if (consumed > 0) {
        m_buf.remove(0, std::min(consumed, m_buf.size()));
        m_pos -= double(consumed);
    }
    return out;
}

QVector<float> Resampler::flush()
{
    if (m_inRate <= 0 || m_outRate <= 0 || m_inRate == m_outRate)
        return {};
    // Push enough silence through the filter delay and the interpolator window
    // to drain everything that was held back.
    const int pad = (m_taps.isEmpty() ? 0 : kTaps / 2) + 3;
    const QVector<float> zeros(pad, 0.0f);
    QVector<float> out = process(zeros.constData(), zeros.size());
    const int inRate = m_inRate, outRate = m_outRate;
    reset(inRate, outRate);
    return out;
}

QVector<float> Resampler::convert(const QVector<float> &in, int inRate, int outRate)
{
    if (inRate == outRate || inRate <= 0 || outRate <= 0)
        return in;
    Resampler r(inRate, outRate);
    QVector<float> out = r.process(in);
    out += r.flush();
    // Trim to the exact expected length so durations are preserved.
    const qsizetype expected = qsizetype(std::llround(double(in.size()) * outRate / inRate));
    if (out.size() > expected)
        out.resize(expected);
    return out;
}
