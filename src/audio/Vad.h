#pragma once

#include <QVector>
#include <functional>

// Energy-based voice activity detector with an adaptive noise floor. Feeds on
// 16 kHz mono float audio and cuts it into utterances. Deliberately simple and
// dependency-free; sensitivity can be raised for whispered or very quiet speech.
class Vad
{
public:
    struct Config
    {
        int sampleRate = 16000;
        int frameMs = 20;
        int sensitivity = 50;      // 0..100
        int minSpeechMs = 250;     // shorter bursts are ignored (clicks, bumps)
        int hangoverMs = 800;      // silence needed to end an utterance
        int preRollMs = 300;       // audio kept from before speech started
        int maxUtteranceMs = 30000;
    };

    Vad();
    explicit Vad(const Config &config);

    void setSensitivity(int percent);
    void reset();
    void process(const float *samples, qsizetype count);
    void process(const QVector<float> &samples) { process(samples.constData(), samples.size()); }
    // Ends any utterance in progress (e.g. when the mic is switched off).
    void flush();

    bool isActive() const { return m_active; }
    float noiseFloorDb() const { return m_noiseDb; }

    std::function<void(bool active)> onActivityChanged;
    std::function<void(const QVector<float> &utterance)> onUtterance;

private:
    void processFrame(const float *frame, int n);
    float thresholdDb() const;

    Config m_cfg;
    int m_frameLen;
    QVector<float> m_partial;   // incomplete frame
    QVector<float> m_preRoll;   // ring of recent frames while idle
    QVector<float> m_utterance;
    float m_noiseDb = -60.0f;
    bool m_active = false;      // inside an utterance (after confirmation)
    int m_speechFrames = 0;     // consecutive loud frames
    int m_silenceFrames = 0;    // consecutive quiet frames while active
    int m_voicedFrames = 0;     // loud frames in the current utterance
};
