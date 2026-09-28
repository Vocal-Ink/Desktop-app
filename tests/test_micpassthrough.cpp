#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "audio/MicPassthrough.h"
#include "support/TestUtil.h"

#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>

using Mode = MicPassthrough::Mode;

namespace {

constexpr int kRate = MicPassthrough::SampleRate;

// A player with main + monitor fake outputs, and a passthrough that never opens a real mic.
struct Rig
{
    AudioPlayer player;
    QList<TestUtil::FakeLane *> lanes;
    MicPassthrough *mic = nullptr;

    Rig()
    {
        player.setLaneFactory([this](const QByteArray &, QObject *parent) {
            auto *lane = new TestUtil::FakeLane(parent, kRate);
            lanes << lane;
            return lane;
        });
        AudioPlayer::Routing r;
        r.mainDevice = "main";
        r.monitorEnabled = true;
        r.monitorDevice = "monitor";
        lanes.clear();
        player.setRouting(r);
        mic = new MicPassthrough(&player, &player);
        mic->setDeviceCaptureEnabled(false);
    }
};

QList<bool> states(const QSignalSpy &spy)
{
    QList<bool> out;
    for (const auto &args : spy)
        out << args.at(0).toBool();
    return out;
}

float maxAbs(const QVector<float> &s, qsizetype from = 0, qsizetype to = -1)
{
    if (to < 0)
        to = s.size();
    float m = 0.0f;
    for (qsizetype i = from; i < to; ++i)
        m = std::max(m, std::fabs(s[i]));
    return m;
}

MicPassthrough::Processor gate(float thresholdDb, float gainDb = 0.0f)
{
    MicPassthrough::Processor p;
    p.setSampleRate(kRate);
    p.setGainDb(gainDb);
    p.setGateDb(thresholdDb);
    p.reset();
    return p;
}

} // namespace

class TestMicPassthrough : public QObject
{
    Q_OBJECT
private slots:
    void modeStrings()
    {
        for (Mode m : {Mode::Off, Mode::HoldKey, Mode::ToggleKey, Mode::AlwaysOn})
            QCOMPARE(MicPassthrough::modeFromString(MicPassthrough::modeToString(m)), m);
        QCOMPARE(MicPassthrough::modeToString(Mode::HoldKey), QStringLiteral("hold"));
        QCOMPARE(MicPassthrough::modeFromString(QStringLiteral("garbage")), Mode::Off);
    }

    void offIsNeverLive()
    {
        Rig rig;
        QSignalSpy live(rig.mic, &MicPassthrough::liveChanged);
        QCOMPARE(rig.mic->mode(), Mode::Off);
        rig.mic->press();
        rig.mic->toggle();
        rig.mic->setLive(true);
        QVERIFY(!rig.mic->isLive());
        QVERIFY(live.isEmpty());
    }

    void holdKey()
    {
        Rig rig;
        QSignalSpy live(rig.mic, &MicPassthrough::liveChanged);
        rig.mic->setMode(Mode::HoldKey);
        QVERIFY(!rig.mic->isLive());
        QVERIFY(live.isEmpty());
        rig.mic->press();
        QVERIFY(rig.mic->isLive());
        rig.mic->press(); // key repeat
        rig.mic->toggle(); // not for this mode
        QVERIFY(rig.mic->isLive());
        rig.mic->release();
        QVERIFY(!rig.mic->isLive());
        rig.mic->release();
        QCOMPARE(states(live), (QList<bool>{true, false}));
    }

    void toggleKey()
    {
        Rig rig;
        QSignalSpy live(rig.mic, &MicPassthrough::liveChanged);
        rig.mic->setMode(Mode::ToggleKey);
        QVERIFY(!rig.mic->isLive());
        rig.mic->toggle();
        QVERIFY(rig.mic->isLive());
        rig.mic->release(); // not for this mode
        rig.mic->press();
        QVERIFY(rig.mic->isLive());
        rig.mic->toggle();
        QVERIFY(!rig.mic->isLive());
        QCOMPARE(states(live), (QList<bool>{true, false}));
    }

    void alwaysOn()
    {
        Rig rig;
        QSignalSpy live(rig.mic, &MicPassthrough::liveChanged);
        rig.mic->setMode(Mode::AlwaysOn);
        QVERIFY(rig.mic->isLive());
        rig.mic->toggle(); // quick mute
        QVERIFY(!rig.mic->isLive());
        rig.mic->setMode(Mode::AlwaysOn); // re-applying settings keeps the mute
        QVERIFY(!rig.mic->isLive());
        rig.mic->toggle();
        QVERIFY(rig.mic->isLive());
        rig.mic->release(); // not for this mode
        QVERIFY(rig.mic->isLive());
        rig.mic->setMode(Mode::HoldKey); // every other mode starts off
        QVERIFY(!rig.mic->isLive());
        rig.mic->setMode(Mode::AlwaysOn);
        rig.mic->setMode(Mode::Off);
        QVERIFY(!rig.mic->isLive());
        QCOMPARE(states(live), (QList<bool>{true, false, true, false, true, false}));
    }

    void liveAudioReachesOnlyTheMainOutput()
    {
        Rig rig;
        const QVector<float> block = TestUtil::sine(300, kRate, 0.01, 0.4f);
        rig.mic->processInput(block, kRate); // not live: dropped
        QCOMPARE(rig.lanes.at(0)->liveFrames, qsizetype(0));

        rig.mic->setMode(Mode::ToggleKey);
        rig.mic->toggle();
        for (int i = 0; i < 10; ++i)
            rig.mic->processInput(block, kRate);
        QCOMPARE(rig.lanes.at(0)->liveFrames, qsizetype(10 * block.size()));
        QCOMPARE(rig.lanes.at(1)->liveFrames, qsizetype(0));
        const QVector<float> out = rig.lanes.at(0)->pull(block.size() * 8);
        QVERIFY(std::fabs(maxAbs(out, block.size() * 2) - 0.4f) < 0.01f); // gate off, 0 dB: unchanged

        rig.mic->setGainDb(6.0f);
        for (int i = 0; i < 10; ++i)
            rig.mic->processInput(block, kRate);
        const QVector<float> louder = rig.lanes.at(0)->pull(block.size() * 8);
        QVERIFY2(std::fabs(maxAbs(louder, block.size()) - 0.4f * AudioConvert::dbToGain(6.0f)) < 0.02f,
                 qPrintable(QString::number(maxAbs(louder, block.size()))));
    }

    void levelIsThrottled()
    {
        Rig rig;
        rig.mic->setMode(Mode::AlwaysOn);
        QSignalSpy level(rig.mic, &MicPassthrough::levelChanged);
        const QVector<float> block = TestUtil::sine(300, kRate, 0.002, 0.5f);
        for (int i = 0; i < 100; ++i) // 100 blocks delivered at once
            rig.mic->processInput(block, kRate);
        QVERIFY(level.size() >= 1 && level.size() <= 2);
        QVERIFY(level.first().at(0).toFloat() > 0.8f); // ~ -9 dBFS on a -60..0 scale
        QTest::qWait(60);
        rig.mic->processInput(block, kRate);
        QVERIFY(level.size() >= 2);
        rig.mic->toggle();
        QCOMPARE(level.last().at(0).toFloat(), 0.0f); // meter drops when muted
    }

    void duckingSettingReachesThePlayer()
    {
        Rig rig;
        rig.mic->setDuckDuringSpeech(true, -18.0f);
        QVERIFY(std::fabs(rig.lanes.at(0)->ducking - AudioConvert::dbToGain(-18.0f)) < 1e-4f);
        rig.mic->setDuckDuringSpeech(false);
        QCOMPARE(rig.lanes.at(0)->ducking, 1.0f);
    }

    void dbConversion()
    {
        QCOMPARE(AudioConvert::dbToGain(0.0f), 1.0f);
        QVERIFY(std::fabs(AudioConvert::dbToGain(-6.0f) - 0.501f) < 0.001f);
        QVERIFY(std::fabs(AudioConvert::dbToGain(20.0f) - 10.0f) < 1e-4f);
    }

    void gainWithoutGate()
    {
        MicPassthrough::Processor p = gate(-90.0f, -6.0f);
        QVector<float> s = TestUtil::sine(200, kRate, 0.05, 0.5f);
        const QVector<float> in = s;
        p.process(s.data(), s.size());
        for (qsizetype i = 0; i < s.size(); ++i)
            QVERIFY(std::fabs(s[i] - in[i] * AudioConvert::dbToGain(-6.0f)) < 1e-6f);
        // Clamped to +-24 dB.
        MicPassthrough::Processor loud = gate(-90.0f, 60.0f);
        QVector<float> one{0.01f};
        loud.process(one.data(), 1);
        QVERIFY(std::fabs(one[0] - 0.01f * AudioConvert::dbToGain(24.0f)) < 1e-5f);
    }

    void gatePassesSpeechAndSilencesNoise()
    {
        // -40 dB threshold: a -10 dBFS tone passes, -60 dBFS hiss is removed.
        MicPassthrough::Processor p = gate(-40.0f);
        QVector<float> loud = TestUtil::sine(200, kRate, 0.2, AudioConvert::dbToGain(-10.0f));
        const QVector<float> in = loud;
        p.process(loud.data(), loud.size());
        QCOMPARE(p.gateGain(), 1.0f);
        const qsizetype settled = kRate / 50; // after the 10 ms attack
        for (qsizetype i = settled; i < loud.size(); ++i)
            QVERIFY(std::fabs(loud[i] - in[i]) < 1e-6f);
        QVERIFY(std::fabs(loud[kRate / 400]) < std::fabs(in[kRate / 400])); // fading in

        MicPassthrough::Processor q = gate(-40.0f);
        QVector<float> hiss = TestUtil::sine(3000, kRate, 0.2, AudioConvert::dbToGain(-60.0f));
        q.process(hiss.data(), hiss.size());
        QCOMPARE(maxAbs(hiss), 0.0f);
        QCOMPARE(q.gateGain(), 0.0f);
    }

    void gateTimingAndNoClicks()
    {
        MicPassthrough::Processor p = gate(-40.0f);
        QVector<float> gains;
        const auto run = [&](float amplitude, double seconds) {
            QVector<float> s(qsizetype(kRate * seconds), amplitude); // DC keeps the maths simple
            for (float &v : s) {
                p.process(&v, 1);
                gains << p.gateGain();
            }
        };
        run(0.2f, 0.1);
        // Attack: fully open within ~10 ms, halfway after ~5 ms.
        QVERIFY(std::fabs(gains.at(kRate / 200) - 0.5f) < 0.05f);
        QCOMPARE(gains.at(kRate / 100 + 1), 1.0f);
        const qsizetype loudEnd = gains.size();
        run(0.0f, 0.4);
        // Hold (~60 ms), then a ~150 ms release.
        QCOMPARE(gains.at(loudEnd + kRate / 25), 1.0f);
        const float mid = gains.at(loudEnd + qsizetype(kRate * 0.16));
        QVERIFY2(mid > 0.1f && mid < 0.9f, qPrintable(QString::number(mid)));
        QCOMPARE(gains.at(loudEnd + qsizetype(kRate * 0.3)), 0.0f);
        // Never more than one attack step per sample: no clicks.
        float maxStep = 0.0f;
        for (qsizetype i = 1; i < gains.size(); ++i)
            maxStep = std::max(maxStep, std::fabs(gains[i] - gains[i - 1]));
        QVERIFY(maxStep <= 1.0f / (0.01f * kRate) + 1e-6f);
    }

    void hysteresisKeepsTheGateSteady()
    {
        // A level wobbling just around the threshold must not chatter.
        MicPassthrough::Processor p = gate(-30.0f);
        QVector<float> s = TestUtil::sine(150, kRate, 1.0, AudioConvert::dbToGain(-29.0f));
        for (qsizetype i = 0; i < s.size(); ++i)
            s[i] *= 1.0f - 0.2f * float(std::sin(double(i) / kRate * 2.0 * 3.14159 * 3.0)); // +-2 dB
        int closes = 0;
        bool wasOpen = false;
        for (float &v : s) {
            p.process(&v, 1);
            if (wasOpen && p.gateGain() < 1.0f)
                ++closes;
            wasOpen = p.gateGain() == 1.0f;
        }
        QVERIFY(wasOpen);
        QCOMPARE(closes, 0);
    }
};

QTEST_GUILESS_MAIN(TestMicPassthrough)
#include "test_micpassthrough.moc"
