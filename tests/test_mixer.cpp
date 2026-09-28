#include "audio/AudioConvert.h"
#include "audio/AudioMixer.h"
#include "audio/AudioPlayer.h"
#include "audio/Earcons.h"
#include "core/SpeechQueue.h"
#include "support/TestUtil.h"
#include "tts/StreamHelpers.h"
#include "tts/TtsRegistry.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kRate = 16000; // fake devices run at the source rate, so sums are exact

// A player with a main and a monitor output, both fake.
struct Rig
{
    AudioPlayer player;
    QList<TestUtil::FakeLane *> lanes;
    int drainDelayMs = 5;

    explicit Rig(bool monitor = true)
    {
        player.setLaneFactory([this](const QByteArray &, QObject *parent) {
            auto *lane = new TestUtil::FakeLane(parent, kRate);
            lane->drainDelayMs = drainDelayMs;
            lanes << lane;
            return lane;
        });
        AudioPlayer::Routing r;
        r.mainDevice = "main";
        r.monitorEnabled = monitor;
        r.monitorDevice = "monitor";
        lanes.clear();
        player.setRouting(r);
        player.setSourceRate(kRate);
    }
    TestUtil::FakeLane *main() const { return lanes.at(0); }
    TestUtil::FakeLane *monitor() const { return lanes.at(1); }
};

QVector<float> constant(qsizetype n, float v)
{
    return QVector<float>(n, v);
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

float maxStep(const QVector<float> &s)
{
    float m = 0.0f;
    for (qsizetype i = 1; i < s.size(); ++i)
        m = std::max(m, std::fabs(s[i] - s[i - 1]));
    return m;
}

// Engine that speaks every chunk as `msPerChunk` of a constant tone.
class ToneEngine : public TtsEngine
{
public:
    explicit ToneEngine(QObject *parent = nullptr)
        : TtsEngine(EngineContext{}, parent)
    {
        Voice v;
        v.engineId = id();
        v.id = QStringLiteral("tone");
        v.name = QStringLiteral("Tone");
        m_voices = {v};
    }
    QString id() const override { return QStringLiteral("tone"); }
    QString displayName() const override { return QStringLiteral("Tone"); }
    bool isLocal() const override { return true; }
    bool isAvailable() const override { return true; }
    void refreshVoices() override { emit voicesChanged(); }
    TtsStream *synthesize(const QString &, const Voice &, const SpeakOptions &) override
    {
        ++requests;
        auto *s = new BufferTtsStream;
        const int ms = msPerChunk;
        QTimer::singleShot(5, s, [s, ms] {
            const QVector<float> samples(kRate * ms / 1000, 0.25f);
            const QAudioFormat fmt = AudioConvert::int16Mono(kRate);
            s->deliverAudio(fmt, AudioConvert::fromMonoFloat(samples.constData(), samples.size(), fmt));
            s->deliverFinished();
        });
        return s;
    }
    int msPerChunk = 250;
    int requests = 0;
};

} // namespace

class TestMixer : public QObject
{
    Q_OBJECT
private slots:
    void speechAndSoundAreSummed()
    {
        Rig rig;
        rig.player.write(constant(1600, 0.25f));
        rig.player.playSound(7, constant(800, 0.2f), kRate, 1.0f);
        const QVector<float> main = rig.main()->pull(1600);
        QVERIFY(std::fabs(main[10] - 0.45f) < 1e-5f);
        QVERIFY(std::fabs(main[799] - 0.45f) < 1e-5f);
        QVERIFY(std::fabs(main[800] - 0.25f) < 1e-5f);
        // Sounds reach the monitor too, with their own gain.
        const QVector<float> monitor = rig.monitor()->pull(1600);
        QVERIFY(std::fabs(monitor[10] - 0.45f) < 1e-5f);
        QCOMPARE(rig.monitor()->soundsPlayed, QList<quint64>{7});
    }

    void soundGainAndResampling()
    {
        Rig rig(false);
        // 0.5 s at 22.05 kHz into a 16 kHz output: resampled, same duration.
        rig.player.playSound(1, TestUtil::sine(440, 22050, 0.5, 0.8f), 22050, 0.5f);
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        QVector<float> out;
        while (finished.isEmpty() && out.size() < kRate * 2)
            out += rig.main()->pull(160);
        QCOMPARE(finished.size(), 1);
        QVERIFY(std::abs(out.size() - kRate / 2) <= 200);
        QVERIFY2(std::fabs(maxAbs(out) - 0.4f) < 0.02f, qPrintable(QString::number(maxAbs(out))));
        const double f = TestUtil::zeroCrossingFrequency(out.mid(400, 6000), kRate);
        QVERIFY2(std::fabs(f - 440.0) < 8.0, qPrintable(QString::number(f)));
    }

    void loudSumsAreSoftClipped()
    {
        Rig rig(false);
        rig.player.write(constant(800, 0.8f));
        rig.player.playSound(1, constant(800, 0.8f), kRate, 1.0f);
        rig.player.playSound(2, constant(400, 0.9f), kRate, 1.0f);
        const QVector<float> out = rig.main()->pull(800);
        QVERIFY(maxAbs(out) <= 1.0f);
        QVERIFY(out[0] > 0.95f);           // 2.5 in: pinned near full scale, no wrap-around
        QVERIFY(out[500] > 0.9f);          // 1.6 in
        QVERIFY(out[0] >= out[500]);       // still monotonic
        QCOMPARE(AudioConvert::softClip(0.5f), 0.5f); // transparent below the knee
        QVERIFY(AudioConvert::softClip(-3.0f) >= -1.0f);
        QCOMPARE(AudioConvert::softClip(std::nanf("")), 0.0f);
    }

    void soundFinishedWaitsForEveryOutput()
    {
        Rig rig;
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        rig.player.playSound(3, constant(480, 0.1f), kRate);
        QVERIFY(rig.player.isSoundPlaying(3));
        rig.main()->pull(1000);
        QCOMPARE(finished.size(), 0); // the monitor hasn't played it yet
        rig.monitor()->pull(1000);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toULongLong(), quint64(3));
        QVERIFY(!rig.player.isSoundPlaying(3));
    }

    void stopSoundFadesAndReportsOnce()
    {
        Rig rig(false);
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        rig.player.playSound(5, constant(kRate, 0.5f), kRate);
        rig.main()->pull(320);
        rig.player.stopSound(5);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!rig.player.isSoundPlaying(5));
        const QVector<float> out = rig.main()->pull(1600);
        QVERIFY(out[0] > 0.4f && out[0] <= 0.5f); // fading, not cut
        QVERIFY(maxStep(out) < 0.01f);            // no click
        QCOMPARE(maxAbs(out, 200), 0.0f);         // gone after ~10 ms
        rig.main()->pull(kRate);
        QCOMPARE(finished.size(), 1);
        rig.player.stopSound(5); // unknown now: nothing happens
        QCOMPARE(finished.size(), 1);
    }

    void replayingRestarts()
    {
        Rig rig(false);
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        rig.player.playSound(9, constant(800, 0.3f), kRate);
        rig.main()->pull(600);
        rig.player.playSound(9, constant(800, 0.3f), kRate);
        const QVector<float> out = rig.main()->pull(700);
        QCOMPARE(finished.size(), 0);             // a restart is not an end
        QVERIFY(std::fabs(out[650] - 0.3f) < 1e-5f); // playing past the first copy's end
        rig.main()->pull(400);
        QCOMPARE(finished.size(), 1);
    }

    void stopAllSounds()
    {
        Rig rig;
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        rig.player.playSound(1, constant(kRate, 0.1f), kRate);
        rig.player.playSound(2, constant(kRate, 0.1f), kRate);
        rig.player.stopAllSounds();
        QCOMPARE(finished.size(), 2);
        rig.main()->pull(400);
        QCOMPARE(maxAbs(rig.main()->pull(400)), 0.0f);
    }

    void emptySoundStillFinishes()
    {
        Rig rig(false);
        QSignalSpy finished(&rig.player, &AudioPlayer::soundFinished);
        rig.player.playSound(4, {}, kRate);
        QCOMPARE(finished.size(), 0); // not from inside the call
        QVERIFY(finished.wait(1000));
    }

    void liveReachesOnlyTheMainOutput()
    {
        Rig rig;
        rig.player.writeLive(constant(1600, 0.3f), kRate);
        QCOMPARE(rig.main()->liveFrames, qsizetype(1600));
        QCOMPARE(rig.monitor()->liveFrames, qsizetype(0));
        const QVector<float> out = rig.main()->pull(1600);
        QVERIFY(out[0] < 0.1f); // eases in
        QVERIFY(std::fabs(out[800] - 0.3f) < 1e-5f);
        QCOMPARE(maxAbs(rig.monitor()->pull(1600)), 0.0f);
        QVERIFY(maxStep(out) < 0.01f);
    }

    void liveWaitsForBacklogAfterUnderrun()
    {
        AudioMixer mixer(kRate);
        const QVector<float> block = constant(160, 0.3f); // 10 ms
        mixer.writeLive(block.constData(), block.size(), kRate);
        QVector<float> out(160);
        mixer.render(out.data(), out.size());
        QCOMPARE(maxAbs(out), 0.0f); // < 40 ms buffered: keep waiting
        for (int i = 0; i < 4; ++i)
            mixer.writeLive(block.constData(), block.size(), kRate);
        mixer.render(out.data(), out.size());
        QVERIFY(maxAbs(out) > 0.29f);
    }

    void liveBufferIsBounded()
    {
        Rig rig(false);
        // One second arrives while the device isn't pulling: only the newest ~150 ms stay.
        QVector<float> ramp(kRate);
        for (qsizetype i = 0; i < ramp.size(); ++i)
            ramp[i] = float(i) / float(kRate) * 0.5f;
        for (qsizetype pos = 0; pos < ramp.size(); pos += 320)
            rig.player.writeLive(ramp.mid(pos, 320), kRate);
        AudioMixer &mixer = rig.main()->mixer;
        QCOMPARE(mixer.maxLiveFrames(), kRate * 150 / 1000);
        QCOMPARE(mixer.pendingLive(), qsizetype(mixer.maxLiveFrames()));
        const QVector<float> out = rig.main()->pull(mixer.maxLiveFrames());
        const float expected = float(kRate - mixer.maxLiveFrames() + 1000) / float(kRate) * 0.5f;
        QVERIFY2(std::fabs(out[1000] - expected) < 1e-3f, qPrintable(QString::number(out[1000])));
    }

    void duckingLowersLiveWhileSpeaking()
    {
        Rig rig(false);
        rig.player.setLiveDuckingDb(-20.0f);
        QCOMPARE(rig.main()->ducking, 0.1f);
        rig.player.write(constant(4800, 0.0f)); // silent speech still counts as speaking
        QVector<float> out;
        for (int i = 0; i < 30; ++i) { // 30 x 10 ms, fed as a microphone would
            rig.player.writeLive(constant(160, 0.3f), kRate);
            if (i >= 4)
                out += rig.main()->pull(160);
        }
        QVERIFY2(std::fabs(out.last() - 0.03f) < 0.002f, qPrintable(QString::number(out.last())));
        QVERIFY(maxStep(out) < 0.02f); // ramped, not switched

        // After speech (and a short hold) the mic comes back up.
        for (int i = 0; i < 60; ++i) {
            rig.player.writeLive(constant(160, 0.3f), kRate);
            out = rig.main()->pull(160);
        }
        QVERIFY2(std::fabs(out.last() - 0.3f) < 1e-4f, qPrintable(QString::number(out.last())));

        rig.player.setLiveDuckingDb(0.0f);
        QCOMPARE(rig.main()->ducking, 1.0f);
        rig.player.setLiveDuckingDb(-120.0f);
        QCOMPARE(rig.main()->ducking, 0.0f);
    }

    void stopFadesSpeechWithoutClick()
    {
        Rig rig(false);
        rig.player.write(constant(1600, 0.5f));
        rig.main()->pull(160);
        rig.player.stop();
        const QVector<float> out = rig.main()->pull(400);
        QVERIFY(out[0] > 0.3f);
        QCOMPARE(maxAbs(out, 100), 0.0f);
        QVERIFY(maxStep(out) < 0.01f);
    }

    void levelFollowsWhatIsHeard()
    {
        Rig rig(false);
        QSignalSpy level(&rig.player, &AudioPlayer::levelChanged);
        rig.player.write(constant(8000, 0.25f));
        QTest::qWait(80);
        QCOMPARE(level.size(), 0); // written ahead is not heard yet
        rig.main()->pull(1600);
        QTRY_VERIFY_WITH_TIMEOUT(!level.isEmpty(), 1000);
        QVERIFY(std::fabs(level.last().at(0).toFloat() - 0.25f) < 1e-4f);
        rig.main()->pull(8000); // the rest, then silence
        QTRY_COMPARE_WITH_TIMEOUT(level.last().at(0).toFloat(), 0.0f, 1000);
        const qsizetype zeros = std::count_if(level.cbegin(), level.cend(),
                                              [](const QList<QVariant> &a) { return a.at(0).toFloat() == 0.0f; });
        QTest::qWait(100);
        QCOMPARE(std::count_if(level.cbegin(), level.cend(),
                               [](const QList<QVariant> &a) { return a.at(0).toFloat() == 0.0f; }),
                 zeros); // 0.0 only once
    }

    void playedPositionFollowsPulls()
    {
        Rig rig(false);
        rig.player.write(constant(kRate, 0.1f));
        QCOMPARE(rig.player.playedMs(), qint64(0));
        rig.main()->pull(kRate / 2);
        QVERIFY2(std::abs(rig.player.playedMs() - 500) <= 20, qPrintable(QString::number(rig.player.playedMs())));
        rig.main()->pull(kRate);
        QCOMPARE(rig.player.playedMs(), qint64(1000));
        // A new utterance starts from zero.
        rig.player.stop();
        rig.player.write(constant(kRate / 4, 0.1f));
        QCOMPARE(rig.player.playedMs(), qint64(0));
        rig.main()->pull(kRate);
        QCOMPARE(rig.player.playedMs(), qint64(250));
    }

    void speechQueueReportsProgress()
    {
        TtsRegistry registry;
        auto *engine = new ToneEngine;
        registry.addEngine(engine);
        Rig rig(false);
        rig.player.setLaneFactory([&rig](const QByteArray &, QObject *parent) {
            auto *lane = new TestUtil::FakeLane(parent, kRate);
            lane->drainDelayMs = 400;
            rig.lanes = {lane};
            return lane;
        });
        SpeechQueue queue(&registry, &rig.player);
        queue.setVoice(engine->voices().first());
        QSignalSpy progress(&queue, &SpeechQueue::progress);
        QSignalSpy finished(&queue, &SpeechQueue::finished);

        // A "device" playing at 4x speed.
        QTimer device;
        device.setInterval(20);
        QObject::connect(&device, &QTimer::timeout, &queue, [&rig] { rig.main()->pull(kRate * 80 / 1000); });
        device.start();

        const quint64 id = queue.say(QStringLiteral("This is the first sentence and it is reasonably long. "
                                                    "Here comes a second sentence that is also fairly long! "
                                                    "And a third one that asks a question about the weather today?"));
        QVERIFY(finished.wait(3000));
        QVERIFY(progress.size() >= 3);
        qint64 lastPlayed = -1, lastTotal = -1;
        bool sawMiddle = false;
        for (const auto &args : std::as_const(progress)) {
            QCOMPARE(args.at(0).toULongLong(), id);
            const qint64 played = args.at(1).toLongLong();
            const qint64 total = args.at(2).toLongLong();
            QVERIFY(played >= lastPlayed);
            QVERIFY(total >= lastTotal);
            QVERIFY(played <= total);
            sawMiddle = sawMiddle || (played > 0 && played < total);
            lastPlayed = played;
            lastTotal = total;
        }
        QVERIFY(sawMiddle);
        const auto last = progress.last();
        QCOMPARE(last.at(1).toLongLong(), last.at(2).toLongLong());
        QCOMPARE(engine->requests, 3);
        QVERIFY2(std::abs(last.at(2).toLongLong() - 750) <= 3, qPrintable(QString::number(last.at(2).toLongLong())));
        QVERIFY(last.at(3).toBool());
        const int count = int(progress.size());
        QTest::qWait(150);
        QCOMPARE(progress.size(), count); // quiet once the message is over
    }

    void earconsAreShortAndSoft()
    {
        const auto cues = {Earcons::Cue::ListenStart, Earcons::Cue::ListenStop, Earcons::Cue::Sent,
                           Earcons::Cue::Error, Earcons::Cue::MicLive, Earcons::Cue::MicMuted,
                           Earcons::Cue::Notify};
        for (Earcons::Cue cue : cues) {
            const QVector<float> s = Earcons::synthesize(cue, 48000);
            const double ms = double(s.size()) * 1000.0 / 48000.0;
            QVERIFY2(ms >= 60.0 && ms <= 250.0, qPrintable(QString::number(ms)));
            const float peak = maxAbs(s);
            QVERIFY(peak > 0.1f && peak < 0.6f);
            QVERIFY(std::fabs(s.first()) < 0.01f && std::fabs(s.last()) < 0.01f);
            QVERIFY(maxStep(s) < 0.12f); // smooth
        }
        QVERIFY(Earcons::synthesize(Earcons::Cue::ListenStart, 48000)
                != Earcons::synthesize(Earcons::Cue::ListenStop, 48000));
    }
};

QTEST_GUILESS_MAIN(TestMixer)
#include "test_mixer.moc"
