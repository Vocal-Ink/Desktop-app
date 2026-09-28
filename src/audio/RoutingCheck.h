#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVector>

// "Will Discord hear me?" — plays a short tone into the voice output and
// listens on the other end of the virtual cable to confirm it arrives.
class RoutingCheck : public QObject
{
    Q_OBJECT
public:
    explicit RoutingCheck(QObject *parent = nullptr);
    ~RoutingCheck() override;

    // An empty `inputDevice` means pairedInputFor(outputDevice). A device that is
    // slow to start gets up to one extra `timeoutMs` to finish playing the tone.
    void start(const QByteArray &outputDevice, const QByteArray &inputDevice, int timeoutMs = 4000);
    void cancel();
    bool isRunning() const;

    // The capture device that carries what is played into `outputDeviceId`
    // (VB-CABLE: "CABLE Output" for "CABLE Input"; BlackHole: same name;
    // Vocal Ink virtual mic; Linux monitor/remap source). Empty if unknown.
    static QByteArray pairedInputFor(const QByteArray &outputDeviceId);
    // The same rule on device descriptions: index into `inputs`, or -1.
    static int pairedInputIndex(const QString &output, const QStringList &inputs);

    // The test signal: two tones in turn (ToneA, ToneB, ToneA), ~1.2 s.
    static constexpr double ToneA = 880.0;
    static constexpr double ToneB = 1320.0;
    static QVector<float> testSignal(int sampleRate);

    // Listens for the test tones in captured audio (pure; exposed for tests).
    class Detector
    {
    public:
        explicit Detector(int sampleRate = 48000);
        void feed(const float *samples, qsizetype count);
        bool heard() const;
        float level() const { return m_level; }         // input level of the latest block, 0..1
        float loudestLevel() const { return m_loudest; } // anything at all arrived?
        int hitsA() const { return m_hitsA; }
        int hitsB() const { return m_hitsB; }
        // Share (0..1) of a block's energy at `freq` (Goertzel filters on the
        // Hann-windowed block, bin and neighbours).
        static float toneShare(const float *block, qsizetype count, double freq, int sampleRate);

    private:
        void analyse(const float *block, qsizetype count);

        int m_rate;
        qsizetype m_blockLen;
        QVector<float> m_block;
        float m_level = 0.0f;
        float m_loudest = 0.0f;
        int m_hitsA = 0;
        int m_hitsB = 0;
    };

signals:
    void progress(float level);                     // 0..1 while listening
    void finished(bool heard, const QString &detail);

private:
    void finish(bool heard, const QString &detail);
    void finishLater(const QString &detail);
    void onCaptured();
    void conclude();
    void teardown();

    class Private;
    Private *d;
};
