#include "audio/AudioConvert.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace AudioConvert {

namespace {

inline float clip(float v)
{
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

inline float readSample(const uchar *p, QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::UInt8:
        return (float(*p) - 128.0f) / 128.0f;
    case QAudioFormat::Int16:
        return float(qFromLittleEndian<qint16>(p)) / 32768.0f;
    case QAudioFormat::Int32:
        return float(double(qFromLittleEndian<qint32>(p)) / 2147483648.0);
    case QAudioFormat::Float: {
        float f;
        const quint32 bits = qFromLittleEndian<quint32>(p);
        std::memcpy(&f, &bits, sizeof f);
        return std::isfinite(f) ? f : 0.0f;
    }
    default:
        return 0.0f;
    }
}

inline void writeSample(uchar *p, QAudioFormat::SampleFormat fmt, float v)
{
    v = clip(v);
    switch (fmt) {
    case QAudioFormat::UInt8:
        *p = uchar(std::lround(v * 127.0f + 128.0f));
        break;
    case QAudioFormat::Int16:
        qToLittleEndian<qint16>(qint16(std::lround(v * 32767.0f)), p);
        break;
    case QAudioFormat::Int32:
        qToLittleEndian<qint32>(qint32(std::llround(double(v) * 2147483647.0)), p);
        break;
    case QAudioFormat::Float: {
        quint32 bits;
        std::memcpy(&bits, &v, sizeof bits);
        qToLittleEndian<quint32>(bits, p);
        break;
    }
    default:
        break;
    }
}

} // namespace

QVector<float> toMonoFloat(const char *data, qsizetype bytes, const QAudioFormat &format)
{
    const int channels = std::max(1, format.channelCount());
    const int bps = format.bytesPerSample();
    if (bps <= 0 || !data)
        return {};
    const qsizetype frameBytes = qsizetype(bps) * channels;
    const qsizetype frames = bytes / frameBytes;
    QVector<float> out(frames);
    const auto *p = reinterpret_cast<const uchar *>(data);
    const auto sf = format.sampleFormat();
    for (qsizetype i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += readSample(p + i * frameBytes + qsizetype(c) * bps, sf);
        out[i] = sum / float(channels);
    }
    return out;
}

QVector<float> toMonoFloat(const QByteArray &pcm, const QAudioFormat &format)
{
    return toMonoFloat(pcm.constData(), pcm.size(), format);
}

QByteArray fromMonoFloat(const float *samples, qsizetype count, const QAudioFormat &format, float gain)
{
    const int channels = std::max(1, format.channelCount());
    const int bps = format.bytesPerSample();
    if (bps <= 0 || count <= 0)
        return {};
    QByteArray out(count * channels * bps, Qt::Uninitialized);
    auto *p = reinterpret_cast<uchar *>(out.data());
    const auto sf = format.sampleFormat();
    for (qsizetype i = 0; i < count; ++i) {
        const float v = samples[i] * gain;
        for (int c = 0; c < channels; ++c) {
            writeSample(p, sf, v);
            p += bps;
        }
    }
    return out;
}

QAudioFormat int16Mono(int rate)
{
    QAudioFormat f;
    f.setSampleRate(rate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    return f;
}

float rms(const float *samples, qsizetype count)
{
    if (count <= 0)
        return 0.0f;
    double acc = 0.0;
    for (qsizetype i = 0; i < count; ++i)
        acc += double(samples[i]) * samples[i];
    return float(std::sqrt(acc / double(count)));
}

float peak(const float *samples, qsizetype count)
{
    float m = 0.0f;
    for (qsizetype i = 0; i < count; ++i)
        m = std::max(m, std::fabs(samples[i]));
    return m;
}

WavHeader parseWavHeader(const QByteArray &bytes)
{
    WavHeader h;
    if (bytes.size() < 12) {
        h.needMoreData = true;
        return h;
    }
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    if (std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0)
        return h; // not a WAV stream

    qsizetype pos = 12;
    bool haveFmt = false;
    while (true) {
        if (pos + 8 > bytes.size()) {
            h.needMoreData = true;
            return h;
        }
        const quint32 chunkSize = qFromLittleEndian<quint32>(p + pos + 4);
        if (std::memcmp(p + pos, "fmt ", 4) == 0) {
            if (pos + 8 + qsizetype(std::min<quint32>(chunkSize, 40)) > bytes.size() || chunkSize < 16) {
                h.needMoreData = chunkSize >= 16;
                return h;
            }
            const uchar *f = p + pos + 8;
            quint16 tag = qFromLittleEndian<quint16>(f);
            const quint16 channels = qFromLittleEndian<quint16>(f + 2);
            const quint32 rate = qFromLittleEndian<quint32>(f + 4);
            const quint16 bits = qFromLittleEndian<quint16>(f + 14);
            if (tag == 0xFFFE && chunkSize >= 40)
                tag = qFromLittleEndian<quint16>(f + 24); // sub-format GUID starts with the tag
            QAudioFormat fmt;
            fmt.setSampleRate(int(rate));
            fmt.setChannelCount(channels);
            if (tag == 1 && bits == 16)
                fmt.setSampleFormat(QAudioFormat::Int16);
            else if (tag == 1 && bits == 32)
                fmt.setSampleFormat(QAudioFormat::Int32);
            else if (tag == 1 && bits == 8)
                fmt.setSampleFormat(QAudioFormat::UInt8);
            else if (tag == 3 && bits == 32)
                fmt.setSampleFormat(QAudioFormat::Float);
            else
                return h; // unsupported encoding (e.g. 24-bit, mu-law)
            h.format = fmt;
            haveFmt = true;
        } else if (std::memcmp(p + pos, "data", 4) == 0) {
            if (!haveFmt)
                return h;
            h.valid = true;
            h.dataOffset = pos + 8;
            h.dataSize = (chunkSize == 0 || chunkSize >= 0x7FFFFFF0u) ? -1 : qint64(chunkSize);
            return h;
        }
        // Chunks are word aligned. Streamed WAVs may report bogus sizes for
        // non-data chunks; guard against runaway offsets.
        const qint64 next = qint64(pos) + 8 + qint64(chunkSize) + (chunkSize & 1u);
        if (next > (qint64(1) << 31))
            return h;
        pos = qsizetype(next);
    }
}

QByteArray makeWav(const QByteArray &pcm, const QAudioFormat &format)
{
    QByteArray out;
    out.reserve(44 + pcm.size());
    auto u32 = [&out](quint32 v) {
        uchar b[4];
        qToLittleEndian(v, b);
        out.append(reinterpret_cast<const char *>(b), 4);
    };
    auto u16 = [&out](quint16 v) {
        uchar b[2];
        qToLittleEndian(v, b);
        out.append(reinterpret_cast<const char *>(b), 2);
    };
    const quint16 channels = quint16(format.channelCount());
    const quint16 bits = quint16(format.bytesPerSample() * 8);
    const quint16 tag = format.sampleFormat() == QAudioFormat::Float ? 3 : 1;
    out.append("RIFF", 4);
    u32(quint32(36 + pcm.size()));
    out.append("WAVE", 4);
    out.append("fmt ", 4);
    u32(16);
    u16(tag);
    u16(channels);
    u32(quint32(format.sampleRate()));
    u32(quint32(format.sampleRate()) * channels * (bits / 8));
    u16(quint16(channels * (bits / 8)));
    u16(bits);
    out.append("data", 4);
    u32(quint32(pcm.size()));
    out.append(pcm);
    return out;
}

QByteArray makeWav16(const QVector<float> &mono, int sampleRate)
{
    const QAudioFormat fmt = int16Mono(sampleRate);
    return makeWav(fromMonoFloat(mono.constData(), mono.size(), fmt), fmt);
}

} // namespace AudioConvert
