#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "core/SpeechQueue.h"
#include "support/TestUtil.h"
#include "tts/StreamHelpers.h"
#include "tts/TtsRegistry.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

// Engine that "speaks" each character as 10 samples of a constant value, after a delay.
class FakeEngine : public TtsEngine
{
public:
    explicit FakeEngine(QObject *parent = nullptr)
        : TtsEngine(EngineContext{}, parent)
    {
        Voice v;
        v.engineId = id();
        v.id = QStringLiteral("robot");
        v.name = QStringLiteral("Robot");
        m_voices = {v};
    }
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    bool isLocal() const override { return true; }
    bool isAvailable() const override { return available; }
    QString unavailableReason() const override { return QStringLiteral("fake is off"); }
    void refreshVoices() override { emit voicesChanged(); }

    TtsStream *synthesize(const QString &text, const Voice &, const SpeakOptions &) override
    {
        requests << text;
        auto *s = new BufferTtsStream;
        const bool fail = text.contains(QLatin1String("FAIL"));
        QTimer::singleShot(delayMs, s, [s, text, fail] {
            if (fail) {
                s->deliverFailed(QStringLiteral("boom"));
                return;
            }
            const QVector<float> samples(text.size() * 10, 0.25f);
            const QAudioFormat fmt = AudioConvert::int16Mono(16000);
            // Deliver in two pieces to exercise streaming.
            const qsizetype half = samples.size() / 2;
            s->deliverAudio(fmt, AudioConvert::fromMonoFloat(samples.constData(), half, fmt));
            s->deliverAudio(fmt, AudioConvert::fromMonoFloat(samples.constData() + half, samples.size() - half, fmt));
            s->deliverFinished();
        });
        return s;
    }

    QStringList requests;
    int delayMs = 5;
    bool available = true;
};

struct Rig
{
    TtsRegistry registry;
    AudioPlayer player;
    FakeEngine *engine = new FakeEngine;
    TestUtil::FakeLane *lane = nullptr;
    SpeechQueue *queue = nullptr;

    Rig()
    {
        registry.addEngine(engine);
        player.setLaneFactory([this](const QByteArray &, QObject *parent) {
            lane = new TestUtil::FakeLane(parent);
            return lane;
        });
        queue = new SpeechQueue(&registry, &player, &registry);
        queue->setVoice(engine->voices().first());
    }
};

} // namespace

class TestSpeechQueue : public QObject
{
    Q_OBJECT
private slots:
    void speaksAndFinishes()
    {
        Rig rig;
        QSignalSpy started(rig.queue, &SpeechQueue::started);
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        QSignalSpy speaking(rig.queue, &SpeechQueue::speakingChanged);

        const quint64 id = rig.queue->say(QStringLiteral("Hello"));
        QVERIFY(id > 0);
        QVERIFY(finished.wait(2000));
        QCOMPARE(started.size(), 1);
        QCOMPARE(finished.first().at(0).toULongLong(), id);
        QCOMPARE(finished.first().at(2).toBool(), true);
        QCOMPARE(rig.lane->samples.size(), 50);
        QCOMPARE(rig.lane->rate, 16000);
        QCOMPARE(speaking.size(), 2);
        QCOMPARE(speaking.at(0).at(0).toBool(), true);
        QCOMPARE(speaking.at(1).at(0).toBool(), false);
    }

    void emptyTextIsIgnored()
    {
        Rig rig;
        QCOMPARE(rig.queue->say(QStringLiteral("   \n ")), quint64(0));
        QVERIFY(rig.engine->requests.isEmpty());
    }

    void longTextIsChunkedInOrder()
    {
        Rig rig;
        rig.queue->setSplitSentences(true);
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        const QString text = QStringLiteral(
            "This is the first sentence and it is reasonably long. "
            "Here comes a second sentence that is also fairly long! "
            "And a third one that asks a question about the weather today?");
        rig.queue->say(text);
        QVERIFY(finished.wait(2000));
        QCOMPARE(rig.engine->requests.size(), 3);
        int expectedChars = 0;
        for (const QString &r : std::as_const(rig.engine->requests))
            expectedChars += int(r.size());
        QCOMPARE(rig.lane->samples.size(), expectedChars * 10);
    }

    void queuesMessagesSequentially()
    {
        Rig rig;
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        rig.queue->say(QStringLiteral("One"));
        rig.queue->say(QStringLiteral("Two"));
        rig.queue->say(QStringLiteral("Three"));
        QCOMPARE(rig.queue->queuedCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 3000);
        QCOMPARE(rig.engine->requests, (QStringList{QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")}));
        for (const auto &args : finished)
            QVERIFY(args.at(2).toBool());
    }

    void stopDropsEverything()
    {
        Rig rig;
        rig.engine->delayMs = 200;
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        rig.queue->say(QStringLiteral("One"));
        rig.queue->say(QStringLiteral("Two"));
        rig.queue->stop();
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.at(0).at(2).toBool(), false);
        QCOMPARE(finished.at(1).at(2).toBool(), false);
        QVERIFY(!rig.queue->isSpeaking());
        QTest::qWait(300);
        QCOMPARE(finished.size(), 2); // cancelled streams stay silent
        QVERIFY(rig.lane->samples.isEmpty());
    }

    void skipContinuesWithNext()
    {
        Rig rig;
        rig.engine->delayMs = 50;
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        rig.queue->say(QStringLiteral("One"));
        rig.queue->say(QStringLiteral("Two"));
        rig.queue->skip();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 2000);
        QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("One"));
        QCOMPARE(finished.at(0).at(2).toBool(), false);
        QCOMPARE(finished.at(1).at(1).toString(), QStringLiteral("Two"));
        QCOMPARE(finished.at(1).at(2).toBool(), true);
    }

    void engineFailureIsReported()
    {
        Rig rig;
        QSignalSpy failed(rig.queue, &SpeechQueue::failed);
        QSignalSpy finished(rig.queue, &SpeechQueue::finished);
        rig.queue->say(QStringLiteral("please FAIL"));
        rig.queue->say(QStringLiteral("after"));
        QVERIFY(failed.wait(2000));
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("boom"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 2000);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("after"));
    }

    void unavailableEngineFailsFast()
    {
        Rig rig;
        rig.engine->available = false;
        QSignalSpy failed(rig.queue, &SpeechQueue::failed);
        rig.queue->say(QStringLiteral("hello"));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(2).toString(), QStringLiteral("fake is off"));
        QVERIFY(!rig.queue->isSpeaking());
    }
};

QTEST_GUILESS_MAIN(TestSpeechQueue)
#include "test_speechqueue.moc"
