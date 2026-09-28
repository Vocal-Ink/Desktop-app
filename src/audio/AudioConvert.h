#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QVector>

#include <algorithm>
#include <cmath>

// Sample-format helpers. Internally all audio is handled as mono float32 in
// [-1, 1]; these convert to/from the interleaved PCM that engines produce and
// devices consume.
namespace AudioConvert {

// Appends `count` samples (QList has no pointer+size append before Qt 6.6).
inline void append(QVector<float> &dst, const float *src, qsizetype count)
{
    if (count <= 0)
        return;
    const qsizetype old = dst.size();
    dst.resize(old + count);
    std::copy(src, src + count, dst.data() + old);
}

// Interleaved PCM -> mono float (channels averaged). Trailing partial frames are ignored.
QVector<float> toMonoFloat(const QByteArray &pcm, const QAudioFormat &format);
QVector<float> toMonoFloat(const char *data, qsizetype bytes, const QAudioFormat &format);

// Mono float -> interleaved PCM in `format`, copying the signal to every channel.
// `gain` is applied and the result clipped.
QByteArray fromMonoFloat(const float *samples, qsizetype count, const QAudioFormat &format, float gain = 1.0f);
// Same, writing into `dst` (count * format.bytesPerFrame() bytes); no allocation.
void writeMonoFloat(const float *samples, qsizetype count, const QAudioFormat &format, char *dst, float gain = 1.0f);

// Transparent below the knee, then bends smoothly towards +-1 (no hard edges
// when several sources add up). Non-finite input becomes silence.
float softClip(float v);

// Decibels -> linear gain.
inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

// A format suitable for 16-bit little-endian mono PCM at `rate`.
QAudioFormat int16Mono(int rate);

// Root-mean-square level of a block, 0..1.
float rms(const float *samples, qsizetype count);
float peak(const float *samples, qsizetype count);

// --- WAV ----------------------------------------------------------------------
struct WavHeader
{
    bool valid = false;
    bool needMoreData = false; // header incomplete; feed more bytes
    QAudioFormat format;
    qsizetype dataOffset = 0;  // bytes before the first sample
    qint64 dataSize = -1;      // -1 when unknown (streamed WAV)
};

// Parses a RIFF/WAVE header (PCM int16/int32/float32 and WAVE_FORMAT_EXTENSIBLE).
// Works on partial streamed input; sizes of 0/0xFFFFFFFF are treated as unknown.
WavHeader parseWavHeader(const QByteArray &bytes);

// Wraps 16-bit PCM into a WAV file (used to upload audio to cloud recognizers).
QByteArray makeWav(const QByteArray &pcm, const QAudioFormat &format);
QByteArray makeWav16(const QVector<float> &mono, int sampleRate);

} // namespace AudioConvert
