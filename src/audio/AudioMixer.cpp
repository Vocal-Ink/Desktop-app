#include "audio/AudioMixer.h"

#include "audio/AudioConvert.h"

#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kSpeechFadeMs = 6;   // when speech is cut off
constexpr int kSoundFadeMs = 10;   // when a sound is stopped or restarted
constexpr int kLivePrefillMs = 40; // backlog wanted before live audio (re)starts
constexpr int kLiveFadeInMs = 5;
constexpr float kLiveDecayMs = 3.0f;
constexpr int kDuckRampMs = 30;
constexpr int kDuckHoldMs = 300;   // bridges the gaps between sentences
constexpr qsizetype kRefillBlock = 4096;
} // namespace

AudioMixer::AudioMixer(int outputRate)
    : m_outRate(outputRate > 0 ? outputRate : 48000)
{
}

int AudioMixer::msToFrames(int ms) const
{
    return std::max(1, int(qint64(ms) * m_outRate / 1000));
}

void AudioMixer::setOutputRate(int rate)
{
    if (rate <= 0)
        return;
    QMutexLocker lock(&m_mutex);
    m_outRate = rate;
    m_speechResampler.reset(m_speechRate, rate);
    m_liveResampler.reset(m_liveRate, rate);
}

// --- Speech ----------------------------------------------------------------------

void AudioMixer::setSpeechRate(int rate)
{
    if (rate == m_speechRate)
        return;
    if (m_speechRate > 0 && !m_speechResampler.isPassthrough()) {
        const QVector<float> tail = m_speechResampler.flush();
        QMutexLocker lock(&m_mutex);
        m_speech += tail;
    }
    m_speechRate = rate;
    m_speechResampler.reset(rate, m_outRate);
}

void AudioMixer::writeSpeech(const float *samples, qsizetype count)
{
    if (count <= 0)
        return;
    if (m_speechRate <= 0)
        setSpeechRate(m_outRate);
    if (m_speechResampler.isPassthrough()) {
        QMutexLocker lock(&m_mutex);
        AudioConvert::append(m_speech, samples, count);
        return;
    }
    const QVector<float> converted = m_speechResampler.process(samples, count);
    QMutexLocker lock(&m_mutex);
    m_speech += converted;
}

void AudioMixer::endSpeech()
{
    if (m_speechRate <= 0 || m_speechResampler.isPassthrough())
        return;
    const QVector<float> tail = m_speechResampler.flush();
    QMutexLocker lock(&m_mutex);
    m_speech += tail;
}

void AudioMixer::clearSpeech()
{
    m_speechResampler.reset(m_speechRate, m_outRate);
    QMutexLocker lock(&m_mutex);
    const qsizetype avail = m_speech.size() - m_speechPos;
    const qsizetype keep = std::min<qsizetype>(avail, msToFrames(kSpeechFadeMs));
    for (qsizetype i = 0; i < keep; ++i)
        m_speech[m_speechPos + i] *= 1.0f - float(i + 1) / float(keep);
    m_speech.resize(m_speechPos + keep);
}

qsizetype AudioMixer::pendingSpeech() const
{
    QMutexLocker lock(&m_mutex);
    return m_speech.size() - m_speechPos;
}

// --- Sounds ------------------------------------------------------------------------

void AudioMixer::playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain)
{
    Voice v;
    v.id = id;
    v.data = mono;
    v.gain = gain;
    if (sampleRate <= 0 || sampleRate == m_outRate) {
        v.pending = mono; // shared, no copy
        v.pos = mono.size();
        v.flushed = true;
    } else {
        v.resampler.reset(sampleRate, m_outRate);
    }
    QMutexLocker lock(&m_mutex);
    for (Voice &old : m_voices) {
        if (old.id == id)
            detach(old);
    }
    m_finished.removeAll(id);
    m_voices.push_back(std::move(v));
}

void AudioMixer::detach(Voice &v)
{
    v.id = 0;
    if (v.fadeLeft < 0) {
        v.fadeLen = msToFrames(kSoundFadeMs);
        v.fadeLeft = v.fadeLen;
    }
}

void AudioMixer::stopSound(quint64 id)
{
    QMutexLocker lock(&m_mutex);
    for (Voice &v : m_voices) {
        if (v.id == id)
            detach(v);
    }
    m_finished.removeAll(id);
}

QList<quint64> AudioMixer::stopAllSounds()
{
    QMutexLocker lock(&m_mutex);
    QList<quint64> ids = m_finished;
    for (Voice &v : m_voices) {
        if (v.id != 0 && !ids.contains(v.id))
            ids.append(v.id);
        detach(v);
    }
    m_finished.clear();
    return ids;
}

bool AudioMixer::isSoundPlaying(quint64 id) const
{
    QMutexLocker lock(&m_mutex);
    return std::any_of(m_voices.begin(), m_voices.end(), [id](const Voice &v) { return v.id == id; });
}

QList<quint64> AudioMixer::takeFinishedSounds()
{
    QMutexLocker lock(&m_mutex);
    QList<quint64> out;
    out.swap(m_finished);
    return out;
}

bool AudioMixer::refill(Voice &v)
{
    if (v.pos < v.data.size()) {
        const qsizetype n = std::min(v.data.size() - v.pos, kRefillBlock);
        v.pending = v.resampler.process(v.data.constData() + v.pos, n);
        v.pendingPos = 0;
        v.pos += n;
        return true;
    }
    if (!v.flushed) {
        v.flushed = true;
        v.pending = v.resampler.flush();
        v.pendingPos = 0;
        return true;
    }
    return false;
}

// Adds the voice to `out`; false once it has nothing left to play.
bool AudioMixer::mixVoice(Voice &v, float *out, qsizetype frames)
{
    qsizetype done = 0;
    while (true) {
        if (v.pendingPos >= v.pending.size()) {
            if (!refill(v))
                return false;
            continue;
        }
        if (done >= frames)
            return true;
        const qsizetype n = std::min(frames - done, v.pending.size() - v.pendingPos);
        const float *src = v.pending.constData() + v.pendingPos;
        for (qsizetype k = 0; k < n; ++k) {
            float g = v.gain;
            if (v.fadeLeft >= 0) {
                if (v.fadeLeft == 0)
                    return false;
                g *= float(v.fadeLeft--) / float(v.fadeLen);
            }
            out[done + k] += src[k] * g;
        }
        done += n;
        v.pendingPos += n;
    }
}

// --- Live input ---------------------------------------------------------------------

void AudioMixer::setMaxLiveMs(int ms)
{
    QMutexLocker lock(&m_mutex);
    m_maxLiveMs = std::max(10, ms);
}

int AudioMixer::maxLiveFrames() const
{
    return msToFrames(m_maxLiveMs);
}

void AudioMixer::writeLive(const float *samples, qsizetype count, int sampleRate)
{
    if (count <= 0 || sampleRate <= 0)
        return;
    if (sampleRate != m_liveRate) {
        m_liveRate = sampleRate;
        m_liveResampler.reset(sampleRate, m_outRate);
    }
    QVector<float> converted;
    if (!m_liveResampler.isPassthrough()) {
        converted = m_liveResampler.process(samples, count);
        samples = converted.constData();
        count = converted.size();
    }
    QMutexLocker lock(&m_mutex);
    AudioConvert::append(m_live, samples, count);
    const qsizetype excess = (m_live.size() - m_livePos) - maxLiveFrames();
    if (excess > 0)
        m_livePos += excess; // drop the oldest: latency matters more than completeness
    compactLocked();
}

void AudioMixer::clearLive()
{
    m_liveResampler.reset(m_liveRate, m_outRate);
    QMutexLocker lock(&m_mutex);
    m_live.clear();
    m_livePos = 0;
}

qsizetype AudioMixer::pendingLive() const
{
    QMutexLocker lock(&m_mutex);
    return m_live.size() - m_livePos;
}

void AudioMixer::setLiveDucking(float gain)
{
    QMutexLocker lock(&m_mutex);
    m_duckGain = std::clamp(gain, 0.0f, 1.0f);
}

// --- Rendering ---------------------------------------------------------------------

void AudioMixer::compactLocked()
{
    if (m_speechPos > 0 && (m_speechPos == m_speech.size() || m_speechPos > 65536)) {
        m_speech.remove(0, m_speechPos);
        m_speechPos = 0;
    }
    if (m_livePos > 0 && (m_livePos == m_live.size() || m_livePos > 65536)) {
        m_live.remove(0, m_livePos);
        m_livePos = 0;
    }
}

void AudioMixer::render(float *out, qsizetype frames)
{
    if (frames <= 0)
        return;
    QMutexLocker lock(&m_mutex);

    const qsizetype speech = std::min(m_speech.size() - m_speechPos, frames);
    if (speech > 0)
        std::copy(m_speech.constData() + m_speechPos, m_speech.constData() + m_speechPos + speech, out);
    std::fill(out + speech, out + frames, 0.0f);
    m_speechPos += speech;

    for (auto it = m_voices.begin(); it != m_voices.end();) {
        if (mixVoice(*it, out, frames)) {
            ++it;
            continue;
        }
        if (it->id != 0)
            m_finished.append(it->id);
        it = m_voices.erase(it);
    }

    // Live input. After an underrun wait for a small backlog before resuming, so
    // capture jitter doesn't turn into crackle; ease in and out instead of jumping.
    const qsizetype liveAvail = m_live.size() - m_livePos;
    if (m_liveStarved && liveAvail >= std::min<qsizetype>(msToFrames(kLivePrefillMs), maxLiveFrames())) {
        m_liveStarved = false;
        m_liveFade = 0;
    }
    const qsizetype live = m_liveStarved ? 0 : std::min(liveAvail, frames);
    const float *src = m_live.constData() + m_livePos;
    const int fadeIn = msToFrames(kLiveFadeInMs);
    const float decay = std::exp(-1000.0f / (kLiveDecayMs * float(m_outRate)));
    const qsizetype hold = msToFrames(kDuckHoldMs);
    const float step = 1.0f / float(msToFrames(kDuckRampMs));

    for (qsizetype i = 0; i < frames; ++i) {
        if (i < speech)
            m_duckHold = hold;
        else if (m_duckHold > 0)
            --m_duckHold;
        const float target = m_duckHold > 0 ? m_duckGain : 1.0f;
        if (m_duckCurrent < target)
            m_duckCurrent = std::min(target, m_duckCurrent + step);
        else if (m_duckCurrent > target)
            m_duckCurrent = std::max(target, m_duckCurrent - step);

        float l;
        if (i < live) {
            l = src[i];
            if (m_liveFade < fadeIn)
                l *= float(++m_liveFade) / float(fadeIn);
            m_liveLast = l;
        } else {
            m_liveLast = std::fabs(m_liveLast) < 1e-6f ? 0.0f : m_liveLast * decay;
            l = m_liveLast;
        }
        out[i] = AudioConvert::softClip(out[i] + l * m_duckCurrent);
    }
    m_livePos += live;
    if (!m_liveStarved && live < frames)
        m_liveStarved = true;
    compactLocked();

    // History, in buckets of kBucketFrames.
    qint64 pos = m_rendered;
    for (qsizetype i = 0; i < frames;) {
        const qint64 bucket = pos / kBucketFrames;
        const qsizetype inBucket = qsizetype(pos % kBucketFrames);
        const qsizetype n = std::min<qsizetype>(frames - i, kBucketFrames - inBucket);
        Bucket &b = m_history[size_t(bucket % kBuckets)];
        if (inBucket == 0) {
            b.peak = 0.0f;
            b.speechBefore = m_speechRendered + std::min(i, speech);
        }
        for (qsizetype k = 0; k < n; ++k)
            b.peak = std::max(b.peak, std::fabs(out[i + k]));
        i += n;
        pos += n;
    }
    m_rendered = pos;
    m_speechRendered += speech;
}

qint64 AudioMixer::renderedFrames() const
{
    QMutexLocker lock(&m_mutex);
    return m_rendered;
}

qint64 AudioMixer::speechFramesQueued() const
{
    QMutexLocker lock(&m_mutex);
    return m_speechRendered + (m_speech.size() - m_speechPos);
}

qint64 AudioMixer::speechFramesBefore(qint64 frame) const
{
    QMutexLocker lock(&m_mutex);
    if (frame >= m_rendered)
        return m_speechRendered;
    if (frame <= 0)
        return 0;
    const qint64 newest = (m_rendered - 1) / kBucketFrames;
    const qint64 bucket = std::max(frame / kBucketFrames, newest - kBuckets + 1);
    return m_history[size_t(bucket % kBuckets)].speechBefore;
}

float AudioMixer::peakBetween(qint64 from, qint64 to) const
{
    QMutexLocker lock(&m_mutex);
    to = std::min(to, m_rendered);
    if (to <= from || to <= 0)
        return 0.0f;
    const qint64 last = (to - 1) / kBucketFrames;
    const qint64 first = std::max(std::max<qint64>(from, 0) / kBucketFrames, last - kBuckets + 1);
    float p = 0.0f;
    for (qint64 b = first; b <= last; ++b)
        p = std::max(p, m_history[size_t(b % kBuckets)].peak);
    return p;
}

bool AudioMixer::isIdle() const
{
    QMutexLocker lock(&m_mutex);
    return m_speech.size() == m_speechPos && m_voices.empty() && m_live.size() == m_livePos
        && m_finished.isEmpty();
}
