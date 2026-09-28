#include "audio/Vad.h"
#include "support/TestUtil.h"

#include <QRandomGenerator>
#include <QTest>

namespace {
QVector<float> noise(int rate, double seconds, float amplitude)
{
    QVector<float> out(qsizetype(rate * seconds));
    auto *rng = QRandomGenerator::global();
    for (float &s : out)
        s = amplitude * float(rng->generateDouble() * 2.0 - 1.0);
    return out;
}
} // namespace

class TestVad : public QObject
{
    Q_OBJECT
private slots:
    void detectsSpeechBurstBetweenSilence()
    {
        Vad vad;
        QList<QVector<float>> utterances;
        QList<bool> activity;
        vad.onUtterance = [&](const QVector<float> &u) { utterances << u; };
        vad.onActivityChanged = [&](bool a) { activity << a; };

        QVector<float> signal = noise(16000, 1.0, 0.002f);             // quiet room
        signal += TestUtil::sine(220, 16000, 1.0, 0.3f);               // "speech"
        signal += noise(16000, 1.5, 0.002f);                           // silence again
        // Feed in odd-sized blocks like a real microphone.
        for (qsizetype pos = 0; pos < signal.size(); pos += 1234)
            vad.process(signal.constData() + pos, std::min<qsizetype>(1234, signal.size() - pos));

        QCOMPARE(utterances.size(), 1);
        QCOMPARE(activity, (QList<bool>{true, false}));
        const double seconds = utterances.first().size() / 16000.0;
        // 1 s of speech + pre-roll (<= 0.3 s) + kept tail (<= 0.25 s)
        QVERIFY2(seconds > 0.95 && seconds < 1.7, qPrintable(QString::number(seconds)));
    }

    void ignoresClicks()
    {
        Vad vad;
        int count = 0;
        vad.onUtterance = [&](const QVector<float> &) { ++count; };
        QVector<float> signal = noise(16000, 1.0, 0.002f);
        signal += TestUtil::sine(1000, 16000, 0.03, 0.8f); // 30 ms click
        signal += noise(16000, 1.5, 0.002f);
        vad.process(signal);
        QCOMPARE(count, 0);
    }

    void sensitivityControlsQuietSpeech()
    {
        QVector<float> signal = noise(16000, 1.0, 0.0005f);
        signal += TestUtil::sine(220, 16000, 1.0, 0.004f); // very quiet (about -51 dBFS rms)
        signal += noise(16000, 1.5, 0.0005f);

        Vad strict;
        strict.setSensitivity(0);
        int strictCount = 0;
        strict.onUtterance = [&](const QVector<float> &) { ++strictCount; };
        strict.process(signal);
        QCOMPARE(strictCount, 0);

        Vad sensitive;
        sensitive.setSensitivity(100);
        int sensitiveCount = 0;
        sensitive.onUtterance = [&](const QVector<float> &) { ++sensitiveCount; };
        sensitive.process(signal);
        QCOMPARE(sensitiveCount, 1);
    }

    void flushEndsUtterance()
    {
        Vad vad;
        int count = 0;
        vad.onUtterance = [&](const QVector<float> &) { ++count; };
        QVector<float> signal = noise(16000, 0.5, 0.002f);
        signal += TestUtil::sine(220, 16000, 0.8, 0.3f);
        vad.process(signal);
        QVERIFY(vad.isActive());
        vad.flush();
        QVERIFY(!vad.isActive());
        QCOMPARE(count, 1);
    }

    void longSpeechIsSegmented()
    {
        Vad::Config cfg;
        cfg.maxUtteranceMs = 2000;
        Vad vad(cfg);
        int count = 0;
        vad.onUtterance = [&](const QVector<float> &u) {
            ++count;
            QVERIFY(u.size() <= 16000 * 2 + 16000 / 2);
        };
        QVector<float> signal = noise(16000, 0.5, 0.002f);
        signal += TestUtil::sine(220, 16000, 5.0, 0.3f);
        signal += noise(16000, 1.5, 0.002f);
        vad.process(signal);
        QVERIFY(count >= 3);
    }
};

QTEST_GUILESS_MAIN(TestVad)
#include "test_vad.moc"
