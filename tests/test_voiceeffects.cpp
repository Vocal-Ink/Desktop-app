#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "audio/VoiceEffects.h"
#include "core/SpeechQueue.h"
#include "support/TestUtil.h"
#include "tts/StreamHelpers.h"
#include "tts/TtsRegistry.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>

using VoiceEffects::Effect;

Q_DECLARE_METATYPE(VoiceEffects::Effect)

namespace {

// Voice-like test signal: a 140 Hz buzz with formant-ish harmonics, fading in and out.
QVector<float> voice(int rate, double seconds)
{
    QVector<float> out(qsizetype(rate * seconds));
    const double pi = 3.14159265358979323846;
    for (qsizetype i = 0; i < out.size(); ++i) {
        const double t = double(i) / rate;
        double v = 0.0;
        for (int h = 1; h <= 25 && 140.0 * h < 0.45 * rate; ++h) {
            const double f = 140.0 * h;
            const double formant = 1.0 / (1.0 + std::pow((f - 700.0) / 350.0, 2.0))
                + 0.6 / (1.0 + std::pow((f - 1900.0) / 450.0, 2.0));
            v += formant * std::sin(2.0 * pi * f * t) / std::sqrt(double(h));
        }
        const double env = std::min({1.0, t / 0.02, (seconds - t) / 0.02});
        out[i] = float(0.25 * v * env);
    }
    return out;
}

// RMS of an effect's steady-state response to a sine.
float response(Effect e, float intensity, double freq, int rate)
{
    VoiceEffects::Chain chain;
    chain.configure(e, intensity, rate);
    QVector<float> s = TestUtil::sine(freq, rate, 0.5, 0.25f);
    chain.process(s.data(), s.size());
    const qsizetype skip = rate / 10;
    return AudioConvert::rms(s.constData() + skip, s.size() - skip);
}

bool allFinite(const QVector<float> &s)
{
    return std::all_of(s.cbegin(), s.cend(), [](float v) { return std::isfinite(v); });
}

// Speaks every character as 10 samples of 0.25 at 16 kHz (like test_speechqueue).
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
    bool isAvailable() const override { return true; }
    void refreshVoices() override { emit voicesChanged(); }
    TtsStream *synthesize(const QString &text, const Voice &, const SpeakOptions &) override
    {
        auto *s = new BufferTtsStream;
        QTimer::singleShot(5, s, [s, text] {
            const QVector<float> samples(text.size() * 10, 0.25f);
            const QAudioFormat fmt = AudioConvert::int16Mono(16000);
            s->deliverAudio(fmt, AudioConvert::fromMonoFloat(samples.constData(), samples.size(), fmt));
            s->deliverFinished();
        });
        return s;
    }
};

} // namespace

class TestVoiceEffects : public QObject
{
    Q_OBJECT
private slots:
    void idsRoundTrip()
    {
        const QList<Effect> effects = VoiceEffects::all();
        QCOMPARE(effects.size(), 8);
        QCOMPARE(effects.first(), Effect::None);
        for (Effect e : effects) {
            QCOMPARE(VoiceEffects::fromId(VoiceEffects::id(e)), e);
            QVERIFY(!VoiceEffects::displayName(e).isEmpty());
            QVERIFY(!VoiceEffects::description(e).isEmpty());
        }
        QCOMPARE(VoiceEffects::fromId(QStringLiteral("no-such-effect")), Effect::None);
        QCOMPARE(VoiceEffects::id(Effect::Radio), QStringLiteral("radio"));
    }

    void effects_data()
    {
        QTest::addColumn<Effect>("effect");
        QTest::addColumn<int>("rate");
        for (Effect e : VoiceEffects::all()) {
            if (e == Effect::None)
                continue;
            for (int rate : {16000, 22050, 48000}) {
                const QByteArray name = VoiceEffects::id(e).toLatin1() + '@' + QByteArray::number(rate);
                QTest::newRow(name.constData()) << e << rate;
            }
        }
    }

    void effects()
    {
        QFETCH(Effect, effect);
        QFETCH(int, rate);
        const QVector<float> in = voice(rate, 0.6);

        // Intensity 0 is bypassed exactly.
        VoiceEffects::Chain dry;
        dry.configure(effect, 0.0f, rate);
        QVERIFY(!dry.isActive());
        QVector<float> same = in;
        dry.process(same.data(), same.size());
        QCOMPARE(same, in);
        QVERIFY(dry.flushTail().isEmpty());

        for (float intensity : {0.3f, 1.0f}) {
            VoiceEffects::Chain chain;
            chain.configure(effect, intensity, rate);
            QVERIFY(chain.isActive());
            QCOMPARE(chain.effect(), effect);
            QVector<float> out = in;
            chain.process(out.data(), out.size());
            QVERIFY(allFinite(out));
            QVERIFY(AudioConvert::peak(out.constData(), out.size()) <= 1.0f);
            float diff = 0.0f;
            for (qsizetype i = 0; i < in.size(); ++i)
                diff = std::max(diff, std::fabs(out[i] - in[i]));
            QVERIFY2(diff > 0.01f, qPrintable(QString::number(diff)));
            // Roughly as loud as the dry voice (within 6 dB).
            const float gainDb = 20.0f * std::log10(AudioConvert::rms(out.constData(), out.size())
                                                    / AudioConvert::rms(in.constData(), in.size()));
            QVERIFY2(std::fabs(gainDb) < 6.0f, qPrintable(QString::number(gainDb)));

            const QVector<float> tail = chain.flushTail();
            QVERIFY(allFinite(tail));
            QVERIFY(tail.size() <= qsizetype(2.5 * rate));
            QVERIFY(tail.isEmpty() || AudioConvert::peak(tail.constData(), tail.size()) <= 1.0f);
            QVERIFY(tail.isEmpty() || std::fabs(tail.last()) < 0.01f);
        }
    }

    void streamingMatchesOneShot()
    {
        for (Effect e : VoiceEffects::all()) {
            const QVector<float> in = voice(24000, 0.4);
            VoiceEffects::Chain a, b;
            a.configure(e, 0.8f, 24000);
            b.configure(e, 0.8f, 24000);
            QVector<float> whole = in;
            a.process(whole.data(), whole.size());
            QVector<float> pieces = in;
            for (qsizetype pos = 0; pos < pieces.size(); pos += 333)
                b.process(pieces.data() + pos, std::min<qsizetype>(333, pieces.size() - pos));
            QCOMPARE(pieces, whole);
        }
    }

    void bandLimitedEffectsCutLowsAndHighs_data()
    {
        QTest::addColumn<Effect>("effect");
        QTest::newRow("radio") << Effect::Radio;
        QTest::newRow("telephone") << Effect::Telephone;
        QTest::newRow("megaphone") << Effect::Megaphone;
    }

    void bandLimitedEffectsCutLowsAndHighs()
    {
        QFETCH(Effect, effect);
        const int rate = 48000;
        const float mid = response(effect, 1.0f, 1000.0, rate);
        const float low = response(effect, 1.0f, 100.0, rate);
        const float high = response(effect, 1.0f, 8000.0, rate);
        QVERIFY(mid > 0.05f);
        QVERIFY2(low < mid * 0.25f, qPrintable(QStringLiteral("%1 vs %2").arg(low).arg(mid))); // > 12 dB down
        QVERIFY2(high < mid * 0.25f, qPrintable(QStringLiteral("%1 vs %2").arg(high).arg(mid)));
        // Absolute, too: well below the 0.177 RMS that went in.
        QVERIFY(low < 0.03f);
        QVERIFY(high < 0.06f);
    }

    void underwaterIsMuffled()
    {
        const float low = response(Effect::Underwater, 1.0f, 300.0, 48000);
        const float high = response(Effect::Underwater, 1.0f, 8000.0, 48000);
        QVERIFY(low > 0.1f);
        QVERIFY(high < low * 0.01f);
    }

    void echoAndCaveRingOn_data()
    {
        QTest::addColumn<Effect>("effect");
        QTest::newRow("echo") << Effect::Echo;
        QTest::newRow("cave") << Effect::Cave;
    }

    void echoAndCaveRingOn()
    {
        QFETCH(Effect, effect);
        const int rate = 22050;
        VoiceEffects::Chain chain;
        chain.configure(effect, 1.0f, rate);
        QVector<float> in = voice(rate, 0.3);
        chain.process(in.data(), in.size());
        const QVector<float> tail = chain.flushTail();
        QVERIFY2(tail.size() > rate / 2, qPrintable(QString::number(tail.size())));
        QVERIFY(tail.size() <= qsizetype(2.5 * rate));
        QVERIFY(AudioConvert::rms(tail.constData(), rate / 2) > 0.01f);
        QVERIFY(std::fabs(tail.last()) < 0.01f);
        // The tail is drained: another flush has nothing left.
        QVERIFY(chain.flushTail().isEmpty());
    }

    void echoRepeats()
    {
        // An impulse comes back every ~280 ms, quieter each time.
        const int rate = 16000;
        VoiceEffects::Chain chain;
        chain.configure(Effect::Echo, 1.0f, rate);
        QVector<float> in(rate / 10, 0.0f);
        for (int i = 0; i < 40; ++i)
            in[i] = 0.8f;
        chain.process(in.data(), in.size());
        const QVector<float> tail = chain.flushTail();
        const qsizetype delay = qsizetype(0.28 * rate) - in.size();
        float previous = 1.0f;
        int audible = 0;
        for (int n = 0; n < 6; ++n) {
            const qsizetype at = delay + qsizetype(n * 0.28 * rate);
            if (at + 60 > tail.size())
                break;
            const float p = AudioConvert::peak(tail.constData() + at, 60);
            QVERIFY2(p < previous, qPrintable(QString::number(p)));
            if (p > 0.8f * 0.02f) // within ~34 dB of the original
                ++audible;
            previous = p;
        }
        QVERIFY2(audible >= 3 && audible <= 5, qPrintable(QString::number(audible)));
    }

    void filterTailIsShort()
    {
        VoiceEffects::Chain chain;
        chain.configure(Effect::Radio, 1.0f, 48000);
        QVector<float> in = voice(48000, 0.2);
        chain.process(in.data(), in.size());
        QVERIFY(chain.flushTail().size() < 48000 / 20);
    }

    void speechQueueAppliesTheEffect()
    {
        TtsRegistry registry;
        auto *engine = new FakeEngine;
        registry.addEngine(engine);
        AudioPlayer player;
        TestUtil::FakeLane *lane = nullptr;
        player.setLaneFactory([&lane](const QByteArray &, QObject *parent) {
            lane = new TestUtil::FakeLane(parent);
            return lane;
        });
        SpeechQueue queue(&registry, &player);
        queue.setVoice(engine->voices().first());
        QSignalSpy finished(&queue, &SpeechQueue::finished);

        queue.setEffect(QStringLiteral("echo"), 1.0f);
        queue.say(QStringLiteral("Hello"));
        QVERIFY(finished.wait(3000));
        QVERIFY2(lane->samples.size() > 50 + 16000 / 4, qPrintable(QString::number(lane->samples.size())));
        QVERIFY(allFinite(lane->samples));
        const QVector<float> echoTail = lane->samples.mid(qsizetype(0.28 * 16000), 50);
        QVERIFY(AudioConvert::peak(echoTail.constData(), echoTail.size()) > 0.1f); // the echo

        lane->samples.clear();
        queue.setEffect(QStringLiteral("radio"), 1.0f);
        queue.say(QStringLiteral("Hello"));
        QVERIFY(finished.wait(3000));
        QVERIFY(lane->samples.size() >= 50);
        QVERIFY(std::fabs(lane->samples.at(40) - 0.25f) > 0.01f); // filtered, not the raw constant

        lane->samples.clear();
        queue.setEffect(QStringLiteral("none"), 1.0f);
        queue.say(QStringLiteral("Hello"));
        QVERIFY(finished.wait(3000));
        QCOMPARE(lane->samples.size(), 50);
        for (float v : std::as_const(lane->samples))
            QVERIFY(std::fabs(v - 0.25f) < 1e-3f);
    }
};

QTEST_GUILESS_MAIN(TestVoiceEffects)
#include "test_voiceeffects.moc"
