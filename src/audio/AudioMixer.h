#pragma once

#include "audio/Resampler.h"

#include <QList>
#include <QMutex>
#include <QVector>
#include <array>
#include <vector>

// Everything one output plays, mixed at the device rate: streamed speech, any
// number of sound effects and the live microphone. The feeding side is used
// from the GUI thread; render() is the device's pull callback and may run on
// an audio thread.
class AudioMixer
{
public:
    explicit AudioMixer(int outputRate = 48000);
    AudioMixer(const AudioMixer &) = delete;
    AudioMixer &operator=(const AudioMixer &) = delete;

    void setOutputRate(int rate); // before rendering starts
    int outputRate() const { return m_outRate; }

    // --- Speech ------------------------------------------------------------------
    void setSpeechRate(int rate);
    void writeSpeech(const float *samples, qsizetype count);
    void endSpeech();   // end of an utterance: releases what the resampler holds back
    void clearSpeech(); // drops pending speech (with a few ms of fade, no click)
    qsizetype pendingSpeech() const;

    // --- Sound effects -------------------------------------------------------------
    // Playing an id that is already playing restarts it.
    void playSound(quint64 id, const QVector<float> &mono, int sampleRate, float gain);
    void stopSound(quint64 id);
    QList<quint64> stopAllSounds(); // returns the ids that were playing
    bool isSoundPlaying(quint64 id) const;
    // Ids that played to the end since the last call.
    QList<quint64> takeFinishedSounds();

    // --- Live input ------------------------------------------------------------------
    // Kept short (see setMaxLiveMs); the oldest audio is dropped on overflow.
    void writeLive(const float *samples, qsizetype count, int sampleRate);
    void clearLive();
    qsizetype pendingLive() const;
    void setMaxLiveMs(int ms);
    int maxLiveFrames() const;
    // Gain applied to live input while speech plays (1 = none), ramped over ~30 ms.
    void setLiveDucking(float gain);

    // Mixes `frames` samples at the output rate, soft-clipped to [-1, 1].
    void render(float *out, qsizetype frames);
    bool isIdle() const;

    // --- Output history -------------------------------------------------------------
    // Positions count output frames rendered since construction. The device plays
    // them a buffer's length later, so meters and progress read the history at
    // the position being heard rather than the one being rendered.
    qint64 renderedFrames() const;
    qint64 speechFramesQueued() const;             // rendered + pending speech
    qint64 speechFramesBefore(qint64 frame) const; // speech among output frames [0, frame)
    float peakBetween(qint64 from, qint64 to) const;

private:
    struct Voice
    {
        quint64 id = 0; // 0 once stopped (fading out)
        QVector<float> data;
        qsizetype pos = 0;
        Resampler resampler;
        QVector<float> pending; // resampled, not yet mixed
        qsizetype pendingPos = 0;
        float gain = 1.0f;
        bool flushed = false;
        int fadeLeft = -1; // frames of fade-out left; -1 while playing
        int fadeLen = 0;
    };
    bool mixVoice(Voice &v, float *out, qsizetype frames);
    bool refill(Voice &v);
    void detach(Voice &v);
    void compactLocked();
    int msToFrames(int ms) const;

    mutable QMutex m_mutex;
    int m_outRate;

    // Main-thread state (resamplers feeding the queues).
    Resampler m_speechResampler;
    int m_speechRate = 0;
    Resampler m_liveResampler;
    int m_liveRate = 0;

    // Shared with render(), guarded by m_mutex.
    QVector<float> m_speech;
    qsizetype m_speechPos = 0;
    std::vector<Voice> m_voices;
    QList<quint64> m_finished;
    QVector<float> m_live;
    qsizetype m_livePos = 0;
    int m_maxLiveMs = 150;
    bool m_liveStarved = true;
    int m_liveFade = 0;
    float m_liveLast = 0.0f;
    float m_duckGain = 1.0f;
    float m_duckCurrent = 1.0f;
    qsizetype m_duckHold = 0;

    static constexpr int kBucketFrames = 256;
    static constexpr int kBuckets = 512;
    struct Bucket
    {
        float peak = 0.0f;
        qint64 speechBefore = 0; // speech frames rendered before the bucket started
    };
    std::array<Bucket, kBuckets> m_history{};
    qint64 m_rendered = 0;
    qint64 m_speechRendered = 0;
};
