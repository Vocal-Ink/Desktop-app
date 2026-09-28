#include "audio/Vad.h"

#include "audio/AudioConvert.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kConfirmFrames = 3;       // loud frames needed to accept speech (60 ms)
constexpr int kPendingSilenceFrames = 6; // quiet frames that cancel an unconfirmed start
constexpr int kTailKeepMs = 250;         // trailing silence kept on each utterance

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
} // namespace

Vad::Vad()
    : Vad(Config{})
{
}

Vad::Vad(const Config &config)
    : m_cfg(config)
    , m_frameLen(std::max(1, config.sampleRate * config.frameMs / 1000))
{
}

void Vad::setSensitivity(int percent)
{
    m_cfg.sensitivity = std::clamp(percent, 0, 100);
}

void Vad::reset()
{
    m_partial.clear();
    m_preRoll.clear();
    m_utterance.clear();
    m_noiseDb = -60.0f;
    if (m_active && onActivityChanged)
        onActivityChanged(false);
    m_active = false;
    m_speechFrames = 0;
    m_silenceFrames = 0;
    m_voicedFrames = 0;
}

float Vad::thresholdDb() const
{
    const float t = float(m_cfg.sensitivity) / 100.0f;
    const float margin = lerp(16.0f, 5.0f, t);      // dB above the noise floor
    const float absolute = lerp(-36.0f, -58.0f, t); // never trigger below this level
    return std::max(m_noiseDb + margin, absolute);
}

void Vad::process(const float *samples, qsizetype count)
{
    qsizetype i = 0;
    if (!m_partial.isEmpty()) {
        const qsizetype need = m_frameLen - m_partial.size();
        const qsizetype take = std::min(need, count);
        AudioConvert::append(m_partial, samples, take);
        i = take;
        if (m_partial.size() == m_frameLen) {
            processFrame(m_partial.constData(), m_frameLen);
            m_partial.clear();
        }
    }
    for (; i + m_frameLen <= count; i += m_frameLen)
        processFrame(samples + i, m_frameLen);
    if (i < count)
        AudioConvert::append(m_partial, samples + i, count - i);
}

void Vad::processFrame(const float *frame, int n)
{
    const float level = AudioConvert::rms(frame, n);
    const float db = 20.0f * std::log10(level + 1e-9f);
    const bool loud = db > thresholdDb();

    if (!m_active && m_speechFrames == 0) {
        // Idle: learn the background level (fast down, slow up).
        if (db < m_noiseDb)
            m_noiseDb = 0.7f * m_noiseDb + 0.3f * db;
        else
            m_noiseDb = 0.995f * m_noiseDb + 0.005f * db;
        m_noiseDb = std::clamp(m_noiseDb, -85.0f, -25.0f);
    }

    const int preRollMax = m_cfg.preRollMs * m_cfg.sampleRate / 1000;

    if (!m_active) {
        if (m_speechFrames == 0) {
            if (loud) {
                // Possible start of speech: begin collecting (with pre-roll).
                m_utterance = m_preRoll;
                AudioConvert::append(m_utterance, frame, n);
                m_speechFrames = 1;
                m_silenceFrames = 0;
            } else {
                AudioConvert::append(m_preRoll, frame, n);
                if (m_preRoll.size() > preRollMax)
                    m_preRoll.remove(0, m_preRoll.size() - preRollMax);
            }
            return;
        }
        // Pending (not yet confirmed).
        AudioConvert::append(m_utterance, frame, n);
        if (loud) {
            ++m_speechFrames;
            m_silenceFrames = 0;
            if (m_speechFrames >= kConfirmFrames) {
                m_active = true;
                m_voicedFrames = m_speechFrames;
                m_preRoll.clear();
                if (onActivityChanged)
                    onActivityChanged(true);
            }
        } else if (++m_silenceFrames >= kPendingSilenceFrames) {
            // A click or bump, not speech.
            m_speechFrames = 0;
            m_silenceFrames = 0;
            m_preRoll = m_utterance.mid(std::max<qsizetype>(0, m_utterance.size() - preRollMax));
            m_utterance.clear();
        }
        return;
    }

    // Active utterance.
    AudioConvert::append(m_utterance, frame, n);
    if (loud) {
        ++m_voicedFrames;
        m_silenceFrames = 0;
    } else {
        ++m_silenceFrames;
    }

    const bool ended = m_silenceFrames * m_cfg.frameMs >= m_cfg.hangoverMs;
    const bool tooLong = qsizetype(m_utterance.size()) * 1000 / m_cfg.sampleRate >= m_cfg.maxUtteranceMs;
    if (ended || tooLong) {
        if (ended) {
            // Keep a little of the trailing silence; drop the rest.
            const qsizetype silence = qsizetype(m_silenceFrames) * m_frameLen;
            const qsizetype keep = qsizetype(kTailKeepMs) * m_cfg.sampleRate / 1000;
            if (silence > keep)
                m_utterance.resize(m_utterance.size() - (silence - keep));
        }
        const bool longEnough = m_voicedFrames * m_cfg.frameMs >= m_cfg.minSpeechMs;
        QVector<float> done;
        done.swap(m_utterance);
        m_speechFrames = 0;
        m_silenceFrames = 0;
        m_voicedFrames = 0;
        if (ended) {
            m_active = false;
            if (onActivityChanged)
                onActivityChanged(false);
        } else {
            m_voicedFrames = 0; // continue the same speech in a new segment
        }
        if (longEnough && onUtterance)
            onUtterance(done);
    }
}

void Vad::flush()
{
    if (m_active) {
        const bool longEnough = m_voicedFrames * m_cfg.frameMs >= m_cfg.minSpeechMs;
        QVector<float> done;
        done.swap(m_utterance);
        m_active = false;
        m_speechFrames = 0;
        m_silenceFrames = 0;
        m_voicedFrames = 0;
        if (onActivityChanged)
            onActivityChanged(false);
        if (longEnough && onUtterance)
            onUtterance(done);
    } else {
        m_utterance.clear();
        m_speechFrames = 0;
        m_silenceFrames = 0;
    }
    m_partial.clear();
}
