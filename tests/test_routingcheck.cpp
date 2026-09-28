#include "audio/AudioConvert.h"
#include "audio/Resampler.h"
#include "audio/RoutingCheck.h"
#include "support/TestUtil.h"

#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <random>

namespace {

QVector<float> noise(int rate, double seconds, float amplitude, unsigned seed = 7)
{
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    QVector<float> out(qsizetype(rate * seconds));
    for (float &v : out)
        v = dist(gen);
    return out;
}

QVector<float> silence(int rate, double seconds)
{
    return QVector<float>(qsizetype(rate * seconds), 0.0f);
}

// What a capture device would deliver: some silence, the test signal (possibly
// resampled and attenuated), a little noise, then silence again.
QVector<float> captured(int rate, float gain, float noiseLevel)
{
    QVector<float> signal = Resampler::convert(RoutingCheck::testSignal(48000), 48000, rate);
    for (float &v : signal)
        v *= gain;
    QVector<float> out = silence(rate, 0.23);
    out += signal;
    out += silence(rate, 0.3);
    const QVector<float> n = noise(rate, double(out.size()) / rate, noiseLevel);
    for (qsizetype i = 0; i < out.size(); ++i)
        out[i] += n[i];
    return out;
}

bool hears(const QVector<float> &audio, int rate, int chunk = 441)
{
    RoutingCheck::Detector detector(rate);
    for (qsizetype pos = 0; pos < audio.size(); pos += chunk)
        detector.feed(audio.constData() + pos, std::min<qsizetype>(chunk, audio.size() - pos));
    return detector.heard();
}

} // namespace

class TestRoutingCheck : public QObject
{
    Q_OBJECT
private slots:
    void toneShare()
    {
        const int rate = 48000;
        const QVector<float> a = TestUtil::sine(RoutingCheck::ToneA, rate, 0.05);
        QVERIFY(RoutingCheck::Detector::toneShare(a.constData(), a.size(), RoutingCheck::ToneA, rate) > 0.95f);
        QVERIFY(RoutingCheck::Detector::toneShare(a.constData(), a.size(), RoutingCheck::ToneB, rate) < 0.01f);
        // Slightly off pitch (a resampling cable) still counts.
        const QVector<float> off = TestUtil::sine(RoutingCheck::ToneA + 7.0, rate, 0.05);
        QVERIFY(RoutingCheck::Detector::toneShare(off.constData(), off.size(), RoutingCheck::ToneA, rate) > 0.8f);
        // Broadband noise has hardly any energy in three bins.
        const QVector<float> n = noise(rate, 0.05, 0.5f);
        QVERIFY(RoutingCheck::Detector::toneShare(n.constData(), n.size(), RoutingCheck::ToneA, rate) < 0.05f);
        // A tone plus equal-power noise is about half and half.
        QVector<float> mix = TestUtil::sine(RoutingCheck::ToneB, rate, 0.05, 0.5f);
        const QVector<float> n2 = noise(rate, 0.05, 0.5f * std::sqrt(1.5f), 3);
        for (qsizetype i = 0; i < mix.size(); ++i)
            mix[i] += n2[i];
        const float share = RoutingCheck::Detector::toneShare(mix.constData(), mix.size(), RoutingCheck::ToneB, rate);
        QVERIFY2(share > 0.35f && share < 0.65f, qPrintable(QString::number(share)));
        const QVector<float> quiet = silence(rate, 0.05);
        QCOMPARE(RoutingCheck::Detector::toneShare(quiet.constData(), quiet.size(), RoutingCheck::ToneA, rate), 0.0f);
    }

    void testSignalShape()
    {
        const QVector<float> s = RoutingCheck::testSignal(48000);
        QCOMPARE(s.size(), 3 * 48000 * 400 / 1000);
        QVERIFY(AudioConvert::peak(s.constData(), s.size()) <= 0.31f);
        QVERIFY(std::fabs(s.first()) < 1e-3f && std::fabs(s.last()) < 0.01f);
    }

    void hearsTheSignal_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<float>("gain");
        QTest::addColumn<float>("noise");
        QTest::newRow("48k clean") << 48000 << 1.0f << 0.0f;
        QTest::newRow("44.1k") << 44100 << 1.0f << 0.001f;
        QTest::newRow("16k") << 16000 << 1.0f << 0.001f;
        QTest::newRow("quiet") << 48000 << 0.01f << 0.0f;                       // -50 dBFS
        QTest::newRow("noisy") << 48000 << 1.0f << 0.1f;                        // ~11 dB SNR
        QTest::newRow("turned down, some hiss") << 32000 << 0.05f << 0.003f;
    }

    void hearsTheSignal()
    {
        QFETCH(int, rate);
        QFETCH(float, gain);
        QFETCH(float, noise);
        QVERIFY(hears(captured(rate, gain, noise), rate));
    }

    void ignoresEverythingElse()
    {
        const int rate = 48000;
        QVERIFY(!hears(silence(rate, 2.0), rate));
        QVERIFY(!hears(noise(rate, 2.0, 0.3f), rate));
        // Only one of the two tones (a hum, a notification sound) isn't enough.
        QVERIFY(!hears(TestUtil::sine(RoutingCheck::ToneA, rate, 2.0), rate));
        QVERIFY(!hears(TestUtil::sine(RoutingCheck::ToneB, rate, 2.0), rate));
        // Nor is a tone far below the noise floor of a digital cable.
        QVERIFY(!hears(captured(rate, 0.0005f, 0.0f), rate));
    }

    void levels()
    {
        const int rate = 48000;
        RoutingCheck::Detector quiet(rate);
        const QVector<float> s = silence(rate, 0.5);
        quiet.feed(s.constData(), s.size());
        QCOMPARE(quiet.loudestLevel(), 0.0f);
        QCOMPARE(quiet.hitsA() + quiet.hitsB(), 0);

        RoutingCheck::Detector loud(rate);
        const QVector<float> n = noise(rate, 0.5, 0.5f);
        loud.feed(n.constData(), n.size());
        QVERIFY(loud.level() > 0.8f);
        QVERIFY(loud.loudestLevel() >= loud.level());
        QVERIFY(!loud.heard());
    }

    void pairing_data()
    {
        QTest::addColumn<QString>("output");
        QTest::addColumn<QStringList>("inputs");
        QTest::addColumn<int>("expected");

        const QStringList windows{QStringLiteral("Microphone (Realtek(R) Audio)"),
                                  QStringLiteral("CABLE Output (VB-Audio Virtual Cable)"),
                                  QStringLiteral("CABLE-A Output (VB-Audio Cable A)"),
                                  QStringLiteral("VoiceMeeter Aux Output (VB-Audio VoiceMeeter AUX VAIO)"),
                                  QStringLiteral("VoiceMeeter Output (VB-Audio VoiceMeeter VAIO)"),
                                  QStringLiteral("Vocal Ink Virtual Mic (Vocal Ink Audio)")};
        QTest::newRow("vb-cable") << QStringLiteral("CABLE Input (VB-Audio Virtual Cable)") << windows << 1;
        QTest::newRow("vb-cable a") << QStringLiteral("CABLE-A Input (VB-Audio Cable A)") << windows << 2;
        QTest::newRow("voicemeeter") << QStringLiteral("VoiceMeeter Input (VB-Audio VoiceMeeter VAIO)") << windows << 4;
        QTest::newRow("voicemeeter aux")
            << QStringLiteral("VoiceMeeter Aux Input (VB-Audio VoiceMeeter AUX VAIO)") << windows << 3;
        QTest::newRow("bundled speaker") << QStringLiteral("Vocal Ink Speaker (Vocal Ink Audio)") << windows << 5;
        QTest::newRow("speakers") << QStringLiteral("Speakers (Realtek(R) Audio)") << windows << -1;
        QTest::newRow("cable not installed")
            << QStringLiteral("CABLE Input (VB-Audio Virtual Cable)") << QStringList{QStringLiteral("Microphone")} << -1;

        const QStringList mac{QStringLiteral("MacBook Pro Microphone"), QStringLiteral("BlackHole 16ch"),
                              QStringLiteral("BlackHole 2ch"), QStringLiteral("CABLE Output"),
                              QStringLiteral("Vocal Ink Virtual Mic")};
        QTest::newRow("blackhole") << QStringLiteral("BlackHole 2ch") << mac << 2;
        QTest::newRow("vb-cable mac") << QStringLiteral("CABLE Input") << mac << 3;
        QTest::newRow("bundled mac") << QStringLiteral("Vocal Ink Virtual Mic") << mac << 4;
        QTest::newRow("mac speakers") << QStringLiteral("MacBook Pro Speakers") << mac << -1;

        const QStringList pulse{QStringLiteral("Monitor of Built-in Audio Analog Stereo"),
                                QStringLiteral("Monitor of Vocal Ink Voice"),
                                QStringLiteral("Built-in Audio Analog Stereo"),
                                QStringLiteral("Vocal Ink Microphone")};
        QTest::newRow("linux remap") << QStringLiteral("Vocal Ink Voice") << pulse << 3;
        QTest::newRow("linux monitor only")
            << QStringLiteral("Vocal Ink Voice") << pulse.mid(0, 3) << 1;
        QTest::newRow("linux speakers") << QStringLiteral("Built-in Audio Analog Stereo") << pulse << -1;
        QTest::newRow("no inputs") << QStringLiteral("Vocal Ink Voice") << QStringList{} << -1;
    }

    void pairing()
    {
        QFETCH(QString, output);
        QFETCH(QStringList, inputs);
        QFETCH(int, expected);
        QCOMPARE(RoutingCheck::pairedInputIndex(output, inputs), expected);
    }

    void unknownDevicesFailGracefully()
    {
        QVERIFY(RoutingCheck::pairedInputFor("no-such-device-id").isEmpty());
        RoutingCheck check;
        QSignalSpy finished(&check, &RoutingCheck::finished);
        check.start("no-such-device-id", "no-such-input-id", 2000);
        QVERIFY(check.isRunning());
        QCOMPARE(finished.size(), 0); // reported asynchronously
        QVERIFY(finished.wait(1000));
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(!finished.first().at(1).toString().isEmpty());
        QVERIFY(!check.isRunning());
        check.cancel(); // harmless when idle
    }

    void cancelSuppressesTheResult()
    {
        RoutingCheck check;
        QSignalSpy finished(&check, &RoutingCheck::finished);
        check.start("no-such-device-id", {}, 2000);
        check.cancel();
        QVERIFY(!check.isRunning());
        QTest::qWait(50);
        QVERIFY(finished.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRoutingCheck)
#include "test_routingcheck.moc"
