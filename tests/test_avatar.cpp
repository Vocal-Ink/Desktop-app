#include "audio/AudioMixer.h"
#include "audio/AudioPlayer.h"
#include "avatar/AvatarController.h"
#include "avatar/StreamerbotSender.h"
#include "avatar/VeadotubeClient.h"
#include "avatar/VmcSender.h"
#include "avatar/VtsClient.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "support/FakeVeadoServer.h"
#include "support/FakeVtsServer.h"
#include "support/TestUtil.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>
#include <QtEndian>
#include <cstring>
#include <memory>

namespace {

using Viseme = AvatarController::Viseme;

struct Rig
{
    QTemporaryDir dir;
    std::unique_ptr<Settings> settings;
    SecretStore secrets{SecretStore::Backend::Memory};
    std::unique_ptr<AvatarController> avatar;

    Rig()
    {
        settings = std::make_unique<Settings>(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        QSignalSpy loaded(&secrets, &SecretStore::loaded);
        secrets.load();
        loaded.wait(1000);
        avatar = std::make_unique<AvatarController>(settings.get(), &secrets);
    }
    void set(const char *key, const QVariant &value) { settings->setValue(key, value); }
    void apply() { avatar->applySettings(); }

    // Feeds a steady speech level, as AudioPlayer does (~30 Hz).
    void feed(float level, int ms)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            avatar->onSpeechLevel(level);
            QTest::qWait(30);
        }
    }
    // Keeps feeding until the condition holds: slow CI machines can go a
    // while between frames.
    template<typename Pred>
    bool feedUntil(float level, Pred done, int timeoutMs = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (!done()) {
            if (t.elapsed() > timeoutMs)
                return false;
            avatar->onSpeechLevel(level);
            QTest::qWait(30);
        }
        return true;
    }
};

struct Receiver
{
    QUdpSocket socket;
    Receiver() { socket.bind(QHostAddress::LocalHost, 0); }
    quint16 port() const { return socket.localPort(); }
    QList<QByteArray> take(int waitMs = 300)
    {
        QList<QByteArray> out;
        (void)QTest::qWaitFor([this] { return socket.hasPendingDatagrams(); }, waitMs);
        QTest::qWait(20);
        while (socket.hasPendingDatagrams())
            out << socket.receiveDatagram().data();
        return out;
    }
};

// {name: value} of the Blend/Val messages in a VMC bundle.
QVariantMap vmcValues(const QByteArray &bundle)
{
    QVariantMap out;
    if (!bundle.startsWith(QByteArray("#bundle\0", 8)))
        return out;
    int pos = 16;
    while (pos + 4 <= bundle.size()) {
        const int size = qFromBigEndian<qint32>(bundle.constData() + pos);
        const QByteArray m = bundle.mid(pos + 4, size);
        pos += 4 + size;
        if (!m.startsWith("/VMC/Ext/Blend/Val"))
            continue;
        // address (20) + ",sf\0" (4) + name (padded) + float
        const int nameStart = 24;
        const int nameEnd = int(m.indexOf('\0', nameStart));
        const QString name = QString::fromUtf8(m.mid(nameStart, nameEnd - nameStart));
        const quint32 bits = qFromBigEndian<quint32>(m.constData() + m.size() - 4);
        float value;
        std::memcpy(&value, &bits, 4);
        out.insert(name, value);
    }
    return out;
}

QJsonObject hotkey(const QString &id)
{
    return {{QStringLiteral("name"), id}, {QStringLiteral("type"), QStringLiteral("TriggerAnimation")},
            {QStringLiteral("file"), QString()}, {QStringLiteral("hotkeyID"), id}};
}

QStringList triggered(const FakeVtsServer &vts)
{
    QStringList ids;
    for (const auto &m : vts.messages(QStringLiteral("HotkeyTriggerRequest")))
        ids << m.data.value(QStringLiteral("hotkeyID")).toString();
    return ids;
}

QList<QPair<QString, bool>> expressionCalls(const FakeVtsServer &vts)
{
    QList<QPair<QString, bool>> out;
    for (const auto &m : vts.messages(QStringLiteral("ExpressionActivationRequest")))
        out.append({m.data.value(QStringLiteral("expressionFile")).toString(), m.data.value(QStringLiteral("active")).toBool()});
    return out;
}

QStringList actionNames(const QList<QByteArray> &datagrams, QList<QJsonObject> *objects = nullptr)
{
    QStringList names;
    for (const QByteArray &d : datagrams) {
        const QJsonObject o = QJsonDocument::fromJson(d).object();
        names << o.value(QStringLiteral("action")).toObject().value(QStringLiteral("name")).toString();
        if (objects)
            objects->append(o);
    }
    return names;
}

// A lane that also reports the speech-only peak, like QtAudioLane.
class SpeechLane : public TestUtil::FakeLane
{
public:
    using FakeLane::FakeLane;
    float takeSpeechPeak() override
    {
        const qint64 heard = mixer.renderedFrames();
        if (heard <= speechFrom)
            return -1.0f;
        const float p = mixer.speechPeakBetween(speechFrom, heard);
        speechFrom = heard;
        return p;
    }
    qint64 speechFrom = 0;
};

constexpr int kRate = 16000;

const char *const kPttOn = R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":true}})";
const char *const kPttOff = R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":false}})";

QString setState(const char *id)
{
    return QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"set","state":"%1"}})")
        .arg(QLatin1String(id));
}

} // namespace

class TestAvatar : public QObject
{
    Q_OBJECT
private slots:
    void visemeTable_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("viseme");
        QTest::newRow("a") << "a" << 0 << "A";
        QTest::newRow("e") << "e" << 0 << "E";
        QTest::newRow("i") << "I" << 0 << "I";
        QTest::newRow("o") << "o" << 0 << "O";
        QTest::newRow("u") << "u" << 0 << "U";
        QTest::newRow("y") << "y" << 0 << "I";
        QTest::newRow("E acute") << QString::fromUtf8("\xC3\x89") << 0 << "E";
        QTest::newRow("u umlaut") << QString::fromUtf8("\xC3\xBC") << 0 << "U";
        QTest::newRow("A ring") << QString::fromUtf8("\xC3\x85") << 0 << "A";
        QTest::newRow("o stroke") << QString::fromUtf8("\xC3\xB8") << 0 << "O";
        QTest::newRow("combining acute") << QStringLiteral("é") << 1 << "E";
        QTest::newRow("space") << "a b" << 1 << "";
        QTest::newRow("period") << "." << 0 << "";
        QTest::newRow("comma") << "hi, you" << 2 << "";
        QTest::newRow("oo") << "moon" << 1 << "U";
        QTest::newRow("oo 2") << "moon" << 2 << "U";
        QTest::newRow("ee") << "see" << 1 << "I";
        QTest::newRow("aw") << "saw" << 1 << "O";
        QTest::newRow("ay") << "day" << 1 << "E";
        QTest::newRow("h looks ahead") << "hello" << 0 << "E";
        QTest::newRow("l looks ahead") << "hello" << 2 << "O";
        QTest::newRow("last consonant looks back") << "cat" << 2 << "A";
        QTest::newRow("across apostrophe") << "don't" << 4 << "O";
        QTest::newRow("w") << "we" << 0 << "U";
        QTest::newRow("qu") << "queen" << 0 << "U";
        QTest::newRow("no vowel") << "hmm" << 1 << "A";
        QTest::newRow("cyrillic") << QString::fromUtf8("\xD0\xBF\xD1\x80\xD0\xB8") << 0 << "A";
        QTest::newRow("cjk") << QString::fromUtf8("\xE6\x97\xA5\xE6\x9C\xAC") << 1 << "A";
        QTest::newRow("digit") << "42" << 0 << "A";
        QTest::newRow("emoji") << QString::fromUtf8("\xF0\x9F\x98\x80") << 0 << "";
        QTest::newRow("emoji low half") << QString::fromUtf8("\xF0\x9F\x98\x80") << 1 << "";
        QTest::newRow("before start") << "a" << -1 << "";
        QTest::newRow("past end") << "a" << 1 << "";
        QTest::newRow("empty") << "" << 0 << "";
    }

    void visemeTable()
    {
        QFETCH(QString, text);
        QFETCH(int, index);
        QFETCH(QString, viseme);
        QCOMPARE(AvatarController::visemeName(AvatarController::visemeAt(text, index)), viseme);
    }

    void helpers()
    {
        QCOMPARE(AvatarController::sourceFromString(QStringLiteral("voiceAndSounds")), AvatarController::Source::VoiceAndSounds);
        QCOMPARE(AvatarController::sourceFromString(QStringLiteral("everything")), AvatarController::Source::Everything);
        QCOMPARE(AvatarController::sourceFromString(QStringLiteral("bogus")), AvatarController::Source::Voice);
        QCOMPARE(AvatarController::micMeterToPeak(0.0f), 0.0f);
        QVERIFY(AvatarController::micMeterToPeak(0.1f) < 0.01f); // -54 dBFS: room noise
        QVERIFY(AvatarController::micMeterToPeak(0.8f) > 0.3f);  // -12 dBFS: talking
        QCOMPARE(AvatarController::formFor(Viseme::Rest, 1.0f), 0.0f);
        QVERIFY(AvatarController::formFor(Viseme::I, 1.0f) > 0.0f);
        QVERIFY(AvatarController::formFor(Viseme::U, 1.0f) < 0.0f);
        QCOMPARE(AvatarController::formFor(Viseme::I, 0.0f), 0.0f);
        QCOMPARE(VtsClient::formToParameter(0.0f), 0.5f);
    }

    void nothingIsOpenedByDefault()
    {
        Rig rig;
        rig.apply();
        const auto activeTimers = [&rig] {
            int n = 0;
            for (QTimer *t : rig.avatar->findChildren<QTimer *>())
                n += t->isActive() ? 1 : 0;
            return n;
        };
        QCOMPARE(activeTimers(), 0);
        QCOMPARE(rig.avatar->vts()->status(), VtsClient::Status::Off);
        QCOMPARE(rig.avatar->veado()->status(), VeadotubeClient::Status::Off);
        QVERIFY(!rig.avatar->vmc()->isEnabled());
        QVERIFY(!rig.avatar->streamerbot()->isEnabled());

        // Everything that can happen, with no target switched on.
        rig.avatar->onSpeechStarted(1, QStringLiteral("hello"));
        rig.feed(0.5f, 100);
        rig.avatar->onMicLiveChanged(true);
        rig.avatar->onMicLevel(0.8f);
        rig.avatar->onSoundStarted(QStringLiteral("x"));
        rig.avatar->onOutputLevel(0.4f);
        rig.avatar->onSpeechFinished(1);
        rig.avatar->onSpeechLevel(0.0f);
        rig.avatar->onMicLiveChanged(false);
        rig.avatar->test(200);
        QVERIFY(rig.avatar->findChildren<QAbstractSocket *>().isEmpty());
        QVERIFY(rig.avatar->findChildren<QWebSocket *>().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 2000);
        QCOMPARE(activeTimers(), 0);
        QVERIFY(rig.avatar->findChildren<QAbstractSocket *>().isEmpty());
    }

    void levelSensitivityAndSmoothing()
    {
        Rig rig;
        rig.set(Keys::AvatarSmoothing, 0);
        rig.apply();
        rig.avatar->onSpeechLevel(0.4f);
        QVERIFY(std::fabs(rig.avatar->mouth() - 0.6) < 1e-4); // 0.4 * 1.5
        rig.avatar->onSpeechLevel(0.01f);                    // below the noise floor
        QTRY_COMPARE(rig.avatar->mouth(), 0.0);

        rig.set(Keys::AvatarSensitivity, 300);
        rig.apply();
        rig.avatar->onSpeechLevel(0.4f);
        QTRY_COMPARE(rig.avatar->mouth(), 1.0); // clamped
        rig.set(Keys::AvatarSensitivity, 20);
        rig.apply();
        rig.avatar->onSpeechLevel(0.4f);
        QTRY_VERIFY(std::fabs(rig.avatar->mouth() - 0.12) < 1e-4);
        rig.avatar->onSpeechLevel(0.0f);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 2000);

        // Heavy smoothing: the mouth opens over several frames.
        rig.set(Keys::AvatarSensitivity, 100);
        rig.set(Keys::AvatarSmoothing, 100);
        rig.apply();
        rig.avatar->onSpeechLevel(0.4f);
        const double first = rig.avatar->mouth();
        QVERIFY2(first > 0.1 && first < 0.2, qPrintable(QString::number(first)));
        QVERIFY(rig.feedUntil(0.4f, [&rig] { return rig.avatar->mouth() > 0.45; }));
        rig.avatar->onSpeechLevel(0.0f);
        QTest::qWait(50);
        QVERIFY(rig.avatar->mouth() > 0.1); // slow release
    }

    void talkingHasHysteresis()
    {
        Rig rig;
        rig.set(Keys::AvatarSmoothing, 0);
        rig.apply();
        QSignalSpy talking(rig.avatar.get(), &AvatarController::talkingChanged);
        rig.avatar->onSpeechLevel(0.1f); // 0.15
        QVERIFY(rig.avatar->isTalking());
        QCOMPARE(talking.size(), 1);

        rig.feed(0.035f, 250); // 0.0525: between "off" and "on"
        QVERIFY(rig.avatar->isTalking());

        rig.feed(0.3f, 60);
        rig.avatar->onSpeechLevel(0.0f); // a short dip
        QTest::qWait(70);
        rig.feed(0.3f, 60);
        QVERIFY(rig.avatar->isTalking());
        QCOMPARE(talking.size(), 1);

        QElapsedTimer quiet;
        rig.avatar->onSpeechLevel(0.0f);
        quiet.start();
        QCOMPARE(rig.avatar->isTalking(), true);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isTalking(), 1000);
        QVERIFY2(quiet.elapsed() >= 120, qPrintable(QString::number(quiet.elapsed())));
        QCOMPARE(talking.size(), 2);
        QCOMPARE(talking.last().at(0).toBool(), false);
    }

    void framesStopAfterSilence()
    {
        Rig rig;
        rig.set(Keys::AvatarSmoothing, 0);
        rig.apply();
        QSignalSpy frames(rig.avatar.get(), &AvatarController::frame);
        QElapsedTimer t;
        t.start();
        for (int i = 0; t.elapsed() < 500; ++i) {
            rig.avatar->onSpeechLevel(i % 2 ? 0.3f : 0.6f);
            QTest::qWait(10);
        }
        const qint64 elapsed = t.elapsed();
        QVERIFY2(frames.size() <= elapsed / 33 + 2, qPrintable(QStringLiteral("%1 frames in %2 ms").arg(frames.size()).arg(elapsed)));
        QVERIFY(frames.size() >= 5);

        rig.avatar->onSpeechLevel(0.0f);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 2000);
        QCOMPARE(rig.avatar->mouth(), 0.0);
        QCOMPARE(rig.avatar->viseme(), QString());
        const qsizetype count = frames.size();
        QTest::qWait(150);
        QCOMPARE(frames.size(), count);

        // An input that stops without its final 0.0 still closes the mouth.
        rig.avatar->onSpeechLevel(0.5f);
        QVERIFY(rig.avatar->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 2000);
        QCOMPARE(rig.avatar->mouth(), 0.0);
    }

    void sourcesPickTheirLevels()
    {
        {
            Rig rig;
            rig.set(Keys::AvatarSmoothing, 0);
            rig.apply();
            rig.avatar->onSpeechLevel(0.0f); // the speech-only level is connected
            rig.avatar->onSpeechStarted(1, QStringLiteral("hi"));
            rig.avatar->onOutputLevel(0.5f); // a sound: not the voice
            QTest::qWait(80);
            QCOMPARE(rig.avatar->mouth(), 0.0);
            rig.avatar->onSpeechFinished(1);
        }
        {
            // Without speechLevelChanged, the output level stands in while speaking.
            Rig rig;
            rig.set(Keys::AvatarSmoothing, 0);
            rig.apply();
            rig.avatar->onOutputLevel(0.5f);
            QVERIFY(!rig.avatar->isRunning());
            rig.avatar->onSpeechStarted(1, QStringLiteral("hi"));
            rig.avatar->onOutputLevel(0.5f);
            QTRY_VERIFY(rig.avatar->mouth() > 0.5);
            rig.avatar->onSpeechFinished(1);
        }
        {
            Rig rig;
            rig.set(Keys::AvatarSmoothing, 0);
            rig.set(Keys::AvatarSource, QStringLiteral("voiceAndSounds"));
            rig.apply();
            QCOMPARE(rig.avatar->source(), AvatarController::Source::VoiceAndSounds);
            rig.avatar->onOutputLevel(0.5f);
            QTRY_VERIFY(rig.avatar->mouth() > 0.5);
            // With the real mic live, the output carries the mic: follow the voice only.
            rig.avatar->onMicLiveChanged(true);
            rig.avatar->onSpeechLevel(0.0f);
            rig.avatar->onOutputLevel(0.5f);
            QTRY_COMPARE(rig.avatar->mouth(), 0.0);
            rig.avatar->onSpeechLevel(0.3f);
            QTRY_VERIFY(rig.avatar->mouth() > 0.4);
        }
        {
            Rig rig;
            rig.set(Keys::AvatarSmoothing, 0);
            rig.set(Keys::AvatarSource, QStringLiteral("everything"));
            rig.apply();
            rig.avatar->onMicLiveChanged(true);
            rig.avatar->onMicLevel(0.1f); // room noise
            QTest::qWait(80);
            QCOMPARE(rig.avatar->mouth(), 0.0);
            rig.avatar->onMicLevel(0.85f);
            QTRY_VERIFY(rig.avatar->mouth() > 0.5);
        }
    }

    void visemesFollowTheText()
    {
        Rig rig;
        rig.set(Keys::AvatarSmoothing, 0);
        rig.apply();
        rig.avatar->onSpeechStarted(1, QStringLiteral("moo ha"));
        const auto viseme = [&rig] { return rig.avatar->viseme(); };
        rig.avatar->onSpeechProgress(1, 0.34); // "o" of "oo"
        QVERIFY(rig.feedUntil(0.5f, [&] { return viseme() == QStringLiteral("U"); }));
        rig.avatar->onSpeechProgress(1, 0.5); // the space: keeps the shape
        rig.feed(0.5f, 150);
        QCOMPARE(viseme(), QStringLiteral("U"));
        rig.avatar->onSpeechProgress(2, 0.9); // another utterance: ignored
        rig.feed(0.5f, 150);
        QCOMPARE(viseme(), QStringLiteral("U"));
        rig.avatar->onSpeechProgress(1, 0.9);
        QVERIFY(rig.feedUntil(0.5f, [&] { return viseme() == QStringLiteral("A"); }));
        QCOMPARE(rig.avatar->visemeValue(), Viseme::A);

        rig.avatar->onSpeechProgress(1, 0.34);
        QVERIFY(rig.feedUntil(0.5f, [&] { return viseme() == QStringLiteral("U"); }));
        rig.set(Keys::AvatarVisemes, false);
        rig.apply();
        QVERIFY(rig.feedUntil(0.5f, [&] { return viseme() == QStringLiteral("A"); })); // open/closed only
        rig.feed(0.5f, 150);
        QCOMPARE(viseme(), QStringLiteral("A"));
        rig.avatar->onSpeechLevel(0.0f);
        QTRY_COMPARE(rig.avatar->viseme(), QString());
        rig.avatar->onSpeechFinished(1);
    }

    void eventsDriveEveryTarget()
    {
        Rig rig;
        FakeVtsServer vts;
        QVERIFY(vts.listen());
        vts.validTokens = {QStringLiteral("tok")};
        vts.hotkeys = {hotkey(QStringLiteral("hkStart")), hotkey(QStringLiteral("hkStop"))};
        rig.secrets.set(Secrets::VTubeStudio, QStringLiteral("tok"));
        Receiver vmc;
        Receiver sbot;
        FakeVeadoServer veado;
        QVERIFY(veado.listen());
        QTemporaryDir instances;
        {
            QFile f(instances.filePath(QStringLiteral("mini-0000018f00000000-00001111")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("time"), QDateTime::currentSecsSinceEpoch()},
                                              {QStringLiteral("name"), QStringLiteral("veadotube mini")},
                                              {QStringLiteral("server"), veado.server()}})
                        .toJson());
        }

        rig.set(Keys::AvatarSmoothing, 0);
        rig.set(Keys::VtsEnabled, true);
        rig.set(Keys::VtsHotkeyStart, QStringLiteral("hkStart"));
        rig.set(Keys::VtsHotkeyStop, QStringLiteral("hkStop"));
        rig.set(Keys::VtsHotkeyMicLive, QStringLiteral("hkLive"));
        rig.set(Keys::VtsHotkeyMicMuted, QStringLiteral("hkMuted"));
        rig.set(Keys::VtsExpression, QStringLiteral("smile.exp3.json"));
        rig.set(Keys::VtsSoundHotkeys, QStringLiteral(R"({"snd1":"hkSound"})"));
        rig.set(Keys::VtsCustomParams, true);
        rig.set(Keys::VmcEnabled, true);
        rig.set(Keys::VmcPort, vmc.port());
        rig.set(Keys::VmcExpression, QStringLiteral("Joy"));
        rig.set(Keys::SbotEnabled, true);
        rig.set(Keys::SbotPort, sbot.port());
        rig.set(Keys::SbotActionMicLive, QStringLiteral("Mic on"));
        rig.set(Keys::VeadoEnabled, true);
        rig.set(Keys::VeadoTalkingState, QStringLiteral("s2"));
        rig.set(Keys::VeadoIdleState, QStringLiteral("s1"));
        rig.set(Keys::VeadoMicLiveState, QStringLiteral("s3"));
        rig.avatar->vts()->setUrlForTesting(vts.url());
        rig.avatar->vts()->setReconnectDelays(50, 200);
        rig.avatar->veado()->setInstancesDirForTesting(instances.path());
        rig.apply();

        QTRY_COMPARE_WITH_TIMEOUT(rig.avatar->vts()->status(), VtsClient::Status::Connected, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(rig.avatar->veado()->status(), VeadotubeClient::Status::Connected, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(vts.count(QStringLiteral("ParameterCreationRequest")), 2, 2000);
        QTRY_VERIFY(rig.avatar->vts()->parameters().contains(QStringLiteral("VocalInkSpeaking")) || vts.count(QStringLiteral("InputParameterListRequest")) >= 1);
        QTest::qWait(100);
        QVERIFY(vmc.take(50).isEmpty()); // nothing moves yet
        veado.clear();

        // Speech starts.
        rig.avatar->onSpeechStarted(5, QStringLiteral("hello there"));
        QTRY_COMPARE(triggered(vts), QStringList{QStringLiteral("hkStart")});
        QTRY_COMPARE(expressionCalls(vts).size(), 1);
        QCOMPARE(expressionCalls(vts).first(), qMakePair(QStringLiteral("smile.exp3.json"), true));
        QList<QJsonObject> actions;
        QCOMPARE(actionNames(sbot.take(), &actions), QStringList{QStringLiteral("Vocal Ink: speaking")});
        QCOMPARE(actions.first().value(QStringLiteral("request")).toString(), QStringLiteral("DoAction"));
        QCOMPARE(actions.first().value(QStringLiteral("args")).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("hello there"));
        QTRY_VERIFY(veado.messages().contains(QLatin1String(kPttOn)));
        QTRY_VERIFY(veado.messages().contains(setState("s2")));

        // The voice is heard.
        rig.feed(0.5f, 150);
        QVERIFY(std::fabs(rig.avatar->mouth() - 0.75) < 1e-4);
        QVERIFY(rig.avatar->isTalking());
        QTRY_VERIFY_WITH_TIMEOUT(!vts.messages(QStringLiteral("InjectParameterDataRequest")).isEmpty(), 1000);
        QTRY_VERIFY(std::fabs(FakeVtsServer::injected(vts.messages(QStringLiteral("InjectParameterDataRequest")).last())
                                  .value(QStringLiteral("MouthOpen")).toDouble() - 0.75) < 0.002);
        const QVariantMap injected = FakeVtsServer::injected(vts.messages(QStringLiteral("InjectParameterDataRequest")).last());
        QCOMPARE(injected.value(QStringLiteral("VocalInkSpeaking")).toDouble(), 1.0);
        QVERIFY(injected.value(QStringLiteral("VoiceE")).toDouble() > 0.7); // "h" of "hello" anticipates the e
        const QList<QByteArray> frames = vmc.take();
        QVERIFY(!frames.isEmpty());
        const QVariantMap last = vmcValues(frames.last());
        QCOMPARE(last.value(QStringLiteral("Joy")).toFloat(), 1.0f);
        QVERIFY(last.value(QStringLiteral("E")).toFloat() > 0.7f);
        QCOMPARE(last.value(QStringLiteral("I")).toFloat() < 0.5f, true);

        // Speech ends.
        veado.clear();
        rig.avatar->onSpeechFinished(5);
        QTRY_COMPARE(triggered(vts), QStringList({QStringLiteral("hkStart"), QStringLiteral("hkStop")}));
        QTRY_COMPARE(expressionCalls(vts).size(), 2);
        QCOMPARE(expressionCalls(vts).last(), qMakePair(QStringLiteral("smile.exp3.json"), false));
        QCOMPARE(actionNames(sbot.take()), QStringList{QStringLiteral("Vocal Ink: done speaking")});
        QTRY_VERIFY(veado.messages().contains(QLatin1String(kPttOff)));
        QTRY_VERIFY(veado.messages().contains(setState("s1")));

        // Silence: one closing frame everywhere, then nothing.
        rig.avatar->onSpeechLevel(0.0f);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 2000);
        QVERIFY(!rig.avatar->vts()->isInControl());
        const QList<QByteArray> closing = vmc.take();
        QVERIFY(!closing.isEmpty());
        const QVariantMap zero = vmcValues(closing.last());
        for (auto it = zero.cbegin(); it != zero.cend(); ++it)
            QCOMPARE(it.value().toFloat(), 0.0f);
        QTRY_COMPARE(FakeVtsServer::injected(vts.messages(QStringLiteral("InjectParameterDataRequest")).last())
                         .value(QStringLiteral("MouthOpen")).toDouble(),
                     0.0);
        const int injects = vts.count(QStringLiteral("InjectParameterDataRequest"));
        QTest::qWait(VtsClient::kKeepAliveMs + 100);
        QCOMPARE(vts.count(QStringLiteral("InjectParameterDataRequest")), injects);
        QVERIFY(vmc.take(50).isEmpty());

        // Mic and sounds.
        veado.clear();
        rig.avatar->onMicLiveChanged(true);
        rig.avatar->onSoundStarted(QStringLiteral("snd1"));
        rig.avatar->onSoundStarted(QStringLiteral("unmapped"));
        QTRY_COMPARE(triggered(vts).mid(2), QStringList({QStringLiteral("hkLive"), QStringLiteral("hkSound")}));
        QCOMPARE(actionNames(sbot.take()), QStringList{QStringLiteral("Mic on")});
        QTRY_VERIFY(veado.messages().contains(setState("s3")));
        rig.avatar->onMicLiveChanged(false);
        QTRY_COMPARE(triggered(vts).mid(4), QStringList{QStringLiteral("hkMuted")});
        QVERIFY(sbot.take(100).isEmpty()); // no action set for "muted"
        QTRY_VERIFY(veado.messages().endsWith(setState("s1")));

        // Panic in the middle of a sentence.
        rig.avatar->onSpeechStarted(6, QStringLiteral("stop me"));
        rig.feed(0.6f, 100);
        QVERIFY(rig.avatar->isTalking());
        veado.clear();
        rig.avatar->onPanic();
        QVERIFY(!rig.avatar->isRunning());
        QVERIFY(!rig.avatar->isTalking());
        QCOMPARE(rig.avatar->mouth(), 0.0);
        QTRY_COMPARE(expressionCalls(vts).last(), qMakePair(QStringLiteral("smile.exp3.json"), false));
        QTRY_VERIFY(veado.messages().contains(QLatin1String(kPttOff)));
        QTRY_COMPARE(FakeVtsServer::injected(vts.messages(QStringLiteral("InjectParameterDataRequest")).last())
                         .value(QStringLiteral("MouthOpen")).toDouble(),
                     0.0);
        const QVariantMap panicFrame = vmcValues(vmc.take().last());
        QCOMPARE(panicFrame.value(QStringLiteral("Joy")).toFloat(), 0.0f);
        QCOMPARE(panicFrame.value(QStringLiteral("E")).toFloat(), 0.0f);
        rig.avatar->onSpeechFinished(6); // from SpeechQueue::stop(): already handled
        QTest::qWait(50);
        QCOMPARE(triggered(vts).count(QStringLiteral("hkStop")), 2);
    }

    void deniedAccessIsNotified()
    {
        Rig rig;
        FakeVtsServer vts;
        QVERIFY(vts.listen());
        vts.denyTokenRequest = true;
        QSignalSpy notify(rig.avatar.get(), &AvatarController::notify);
        rig.set(Keys::VtsEnabled, true);
        rig.avatar->vts()->setUrlForTesting(vts.url());
        rig.apply();
        QTRY_COMPARE_WITH_TIMEOUT(rig.avatar->vts()->status(), VtsClient::Status::Denied, 3000);
        QCOMPARE(notify.size(), 1);
        QCOMPARE(notify.first().at(1).toInt(), 1);
    }

    void testMovesTheMouth()
    {
        Rig rig;
        Receiver vmc;
        rig.set(Keys::VmcEnabled, true);
        rig.set(Keys::VmcPort, vmc.port());
        rig.apply();
        QSignalSpy frames(rig.avatar.get(), &AvatarController::frame);
        QSignalSpy talking(rig.avatar.get(), &AvatarController::talkingChanged);
        double widest = 0.0;
        QStringList shapes;
        connect(rig.avatar.get(), &AvatarController::frame, this, [&] {
            widest = std::max(widest, rig.avatar->mouth());
            if (!shapes.contains(rig.avatar->viseme()))
                shapes << rig.avatar->viseme();
        });
        rig.avatar->test(450);
        QVERIFY(rig.avatar->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.avatar->isRunning(), 3000);
        QVERIFY(widest > 0.4);
        QVERIFY(shapes.size() >= 3);
        QVERIFY(talking.size() >= 2);
        QCOMPARE(rig.avatar->mouth(), 0.0);
        QVERIFY(vmc.take().size() >= 5);
    }

    void streamerbotPayloads()
    {
        const QByteArray payload = StreamerbotSender::actionPayload(QStringLiteral("Scene \"A\""),
                                                                    {{QStringLiteral("text"), QStringLiteral("hi")}},
                                                                    QStringLiteral("7"));
        const QJsonObject o = QJsonDocument::fromJson(payload).object();
        QCOMPARE(o.value(QStringLiteral("request")).toString(), QStringLiteral("DoAction"));
        QCOMPARE(o.value(QStringLiteral("action")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Scene \"A\""));
        QCOMPARE(o.value(QStringLiteral("args")).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("hi"));
        QCOMPARE(o.value(QStringLiteral("id")).toString(), QStringLiteral("7"));
        QVERIFY(!payload.contains('\n'));

        Receiver rx;
        StreamerbotSender sender;
        sender.setTarget(QStringLiteral("127.0.0.1"), rx.port());
        sender.doAction(QStringLiteral("Off")); // disabled
        QVERIFY(rx.take(100).isEmpty());
        sender.setEnabled(true);
        sender.doAction(QString());
        sender.doAction(QStringLiteral("   "));
        QVERIFY(rx.take(100).isEmpty());
        sender.doAction(QStringLiteral("Go"));
        sender.doAction(QStringLiteral("Again"), {{QStringLiteral("n"), 2}});
        const QList<QByteArray> got = rx.take();
        QCOMPARE(got.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(got.at(0)).object().value(QStringLiteral("id")).toString(), QStringLiteral("1"));
        const QJsonObject second = QJsonDocument::fromJson(got.at(1)).object();
        QCOMPARE(second.value(QStringLiteral("id")).toString(), QStringLiteral("2"));
        QCOMPARE(second.value(QStringLiteral("args")).toObject().value(QStringLiteral("n")).toInt(), 2);
    }

    void mixerKeepsTheSpeechPeakApart()
    {
        // History is kept in buckets of 256 frames: use whole buckets.
        AudioMixer mixer(kRate);
        mixer.setSpeechRate(kRate);
        const QVector<float> speech(2048, 0.25f);
        mixer.writeSpeech(speech.constData(), speech.size());
        mixer.playSound(1, QVector<float>(4096, 0.5f), kRate, 1.0f);
        QVector<float> out(2048);
        mixer.render(out.data(), out.size());
        QCOMPARE(mixer.speechPeakBetween(0, 2048), 0.25f);
        QVERIFY(mixer.peakBetween(0, 2048) > 0.5f);
        mixer.render(out.data(), out.size()); // the sound alone
        QCOMPARE(mixer.speechPeakBetween(2048, 4096), 0.0f);
        QVERIFY(mixer.peakBetween(2048, 4096) > 0.4f);
        QCOMPARE(mixer.speechPeakBetween(0, 4096), 0.25f);
    }

    void playerReportsSpeechOnlyLevel()
    {
        AudioPlayer player;
        SpeechLane *lane = nullptr;
        player.setLaneFactory([&lane](const QByteArray &, QObject *parent) {
            lane = new SpeechLane(parent, kRate);
            return lane;
        });
        AudioPlayer::Routing r;
        r.mainDevice = "main";
        player.setRouting(r);
        player.setSourceRate(kRate);
        QVERIFY(lane);

        QSignalSpy speech(&player, &AudioPlayer::speechLevelChanged);
        QSignalSpy level(&player, &AudioPlayer::levelChanged);
        player.write(QVector<float>(4800, 0.25f));
        player.playSound(9, QVector<float>(kRate, 0.5f), kRate, 1.0f);
        lane->pull(1600);
        QTRY_VERIFY_WITH_TIMEOUT(!speech.isEmpty(), 1000);
        QVERIFY(std::fabs(speech.last().at(0).toFloat() - 0.25f) < 1e-4f);
        QTRY_VERIFY(!level.isEmpty());
        QVERIFY(level.last().at(0).toFloat() > 0.5f); // speech and sound together

        lane->pull(3200); // the rest of the speech
        QTest::qWait(80);
        QCOMPARE(speech.last().at(0).toFloat() > 0.2f, true);
        const qsizetype levelsBefore = level.size();
        lane->pull(1600); // the sound goes on alone
        QTRY_COMPARE_WITH_TIMEOUT(speech.last().at(0).toFloat(), 0.0f, 1000);
        bool soundHeard = false;
        for (qsizetype i = levelsBefore; i < level.size(); ++i)
            soundHeard = soundHeard || level.at(i).at(0).toFloat() > 0.4f;
        QVERIFY(soundHeard);
        const auto zeros = [&speech] {
            return std::count_if(speech.cbegin(), speech.cend(), [](const QList<QVariant> &a) { return a.at(0).toFloat() == 0.0f; });
        };
        lane->pull(1600);
        QTest::qWait(300); // more sound, then silence
        QCOMPARE(zeros(), 1); // 0.0 only once
    }

    void lanesWithoutSpeechPeakStayQuiet()
    {
        AudioPlayer player;
        TestUtil::FakeLane *lane = nullptr;
        player.setLaneFactory([&lane](const QByteArray &, QObject *parent) {
            lane = new TestUtil::FakeLane(parent, kRate);
            return lane;
        });
        AudioPlayer::Routing r;
        r.mainDevice = "main";
        player.setRouting(r);
        player.setSourceRate(kRate);
        QSignalSpy speech(&player, &AudioPlayer::speechLevelChanged);
        QSignalSpy level(&player, &AudioPlayer::levelChanged);
        player.write(QVector<float>(3200, 0.25f));
        lane->pull(3200);
        QTRY_VERIFY_WITH_TIMEOUT(!level.isEmpty(), 1000);
        QTest::qWait(100);
        QCOMPARE(speech.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAvatar)
#include "test_avatar.moc"
