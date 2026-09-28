#include "avatar/VtsClient.h"
#include "core/SecretStore.h"
#include "support/FakeVtsServer.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QUdpSocket>
#include <memory>

namespace {

using Status = VtsClient::Status;

struct Rig
{
    FakeVtsServer server;
    SecretStore secrets{SecretStore::Backend::Memory};
    std::unique_ptr<VtsClient> client;

    explicit Rig(bool loadSecrets = true)
    {
        server.listen();
        if (loadSecrets) {
            QSignalSpy loaded(&secrets, &SecretStore::loaded);
            secrets.load();
            loaded.wait(1000);
        }
        client = std::make_unique<VtsClient>(&secrets);
        client->setUrlForTesting(server.url());
        client->setReconnectDelays(50, 200);
    }
    bool waitFor(Status status, int ms = 3000)
    {
        return QTest::qWaitFor([this, status] { return client->status() == status; }, ms);
    }
    bool connect()
    {
        client->setEnabled(true);
        return waitFor(Status::Connected);
    }
};

std::array<float, 5> vowels(float a = 0, float i = 0, float u = 0, float e = 0, float o = 0)
{
    return {a, i, u, e, o};
}

QJsonObject hotkey(const QString &id, const QString &name)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("type"), QStringLiteral("TriggerAnimation")},
            {QStringLiteral("description"), QStringLiteral("Play an animation")},
            {QStringLiteral("file"), name + QStringLiteral(".motion3.json")},
            {QStringLiteral("hotkeyID"), id}};
}

// A TCP port nobody listens on (for "VTube Studio isn't running").
quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QByteArray png(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

quint16 freeUdpPort()
{
    QUdpSocket probe;
    probe.bind(QHostAddress::LocalHost, 0);
    return probe.localPort();
}

void broadcast(quint16 port, bool active, int apiPort)
{
    const QJsonObject message{
        {QStringLiteral("apiName"), QStringLiteral("VTubeStudioPublicAPI")},
        {QStringLiteral("apiVersion"), QStringLiteral("1.0")},
        {QStringLiteral("timestamp"), 1},
        {QStringLiteral("messageType"), QStringLiteral("VTubeStudioAPIStateBroadcast")},
        {QStringLiteral("requestID"), QStringLiteral("x")},
        {QStringLiteral("data"),
         QJsonObject{{QStringLiteral("active"), active},
                     {QStringLiteral("port"), apiPort},
                     {QStringLiteral("instanceID"), QStringLiteral("abc")},
                     {QStringLiteral("windowTitle"), QStringLiteral("VTube Studio")}}}};
    QUdpSocket s;
    s.writeDatagram(QJsonDocument(message).toJson(QJsonDocument::Compact), QHostAddress::LocalHost, port);
}

} // namespace

class TestVts : public QObject
{
    Q_OBJECT
private slots:
    void offByDefault()
    {
        Rig rig;
        QCOMPARE(rig.client->status(), Status::Off);
        QTest::qWait(100);
        QCOMPARE(rig.server.clientCount(), 0);
        QVERIFY(rig.client->findChildren<QAbstractSocket *>().isEmpty());
    }

    void tokenRequestStoresToken()
    {
        Rig rig;
        rig.server.holdTokenRequest = true;
        const QByteArray icon = png(128);
        rig.client->setPluginIcon(icon);
        rig.client->setEnabled(true);
        QVERIFY(rig.waitFor(Status::WaitingForAllow));
        QTRY_COMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 1);
        const auto requests = rig.server.messages(QStringLiteral("AuthenticationTokenRequest"));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().data.value(QStringLiteral("pluginName")).toString(), QStringLiteral("Vocal Ink"));
        QCOMPARE(requests.first().data.value(QStringLiteral("pluginDeveloper")).toString(), QStringLiteral("Vocal Ink"));
        QCOMPARE(requests.first().data.value(QStringLiteral("pluginIcon")).toString(), QString::fromLatin1(icon.toBase64()));

        // No timeout: still waiting after a while.
        QTest::qWait(300);
        QCOMPARE(rig.client->status(), Status::WaitingForAllow);

        rig.server.allowPending();
        QVERIFY(rig.waitFor(Status::Connected));
        QCOMPARE(rig.secrets.get(Secrets::VTubeStudio), QStringLiteral("token-1"));
        const auto auth = rig.server.messages(QStringLiteral("AuthenticationRequest"));
        QCOMPARE(auth.size(), 1);
        QCOMPARE(auth.first().data.value(QStringLiteral("authenticationToken")).toString(), QStringLiteral("token-1"));
    }

    void storedTokenIsUsedOnReconnect()
    {
        Rig rig;
        rig.secrets.set(Secrets::VTubeStudio, QStringLiteral("saved"));
        rig.server.validTokens = {QStringLiteral("saved")};
        QVERIFY(rig.connect());
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 0);

        rig.server.disconnectClients(); // VTS restarts
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("AuthenticationRequest")), 2, 3000);
        QVERIFY(rig.waitFor(Status::Connected));
        for (const auto &m : rig.server.messages(QStringLiteral("AuthenticationRequest")))
            QCOMPARE(m.data.value(QStringLiteral("authenticationToken")).toString(), QStringLiteral("saved"));
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 0);

        rig.client->reconnect();
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("AuthenticationRequest")), 3, 3000);
        QVERIFY(rig.waitFor(Status::Connected));
    }

    void deniedStopsAsking()
    {
        Rig rig;
        rig.server.denyTokenRequest = true;
        QSignalSpy denied(rig.client.get(), &VtsClient::accessDenied);
        rig.client->setEnabled(true);
        QVERIFY(rig.waitFor(Status::Denied));
        QCOMPARE(denied.size(), 1);
        QVERIFY(rig.client->detail().contains(QStringLiteral("50")));
        QTest::qWait(400); // no reconnect loop, no new popups
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 1);
        QCOMPARE(rig.client->status(), Status::Denied);

        // "Ask again"
        rig.server.denyTokenRequest = false;
        rig.client->requestAccess();
        QVERIFY(rig.waitFor(Status::Connected));
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 2);
        QCOMPARE(rig.secrets.get(Secrets::VTubeStudio), QStringLiteral("token-1"));
    }

    void revokedTokenIsCleared()
    {
        Rig rig;
        rig.secrets.set(Secrets::VTubeStudio, QStringLiteral("revoked"));
        QSignalSpy denied(rig.client.get(), &VtsClient::accessDenied);
        rig.client->setEnabled(true);
        QVERIFY(rig.waitFor(Status::Denied));
        QVERIFY(!rig.secrets.has(Secrets::VTubeStudio));
        QCOMPARE(denied.size(), 1);
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 0); // no surprise popup
    }

    void forgetAccessDropsTheToken()
    {
        Rig rig;
        QVERIFY(rig.connect());
        QVERIFY(rig.secrets.has(Secrets::VTubeStudio));
        QSignalSpy denied(rig.client.get(), &VtsClient::accessDenied);
        rig.client->forgetAccess();
        QVERIFY(!rig.secrets.has(Secrets::VTubeStudio));
        QCOMPARE(rig.client->status(), Status::Denied);
        QCOMPARE(denied.size(), 0); // our own doing, not VTS's
        QTest::qWait(200);
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 1);
    }

    void waitsForTheSecretStore()
    {
        Rig rig(false);
        rig.secrets.set(Secrets::VTubeStudio, QStringLiteral("saved")); // as if it were still loading
        rig.server.validTokens = {QStringLiteral("saved")};
        rig.client->setEnabled(true);
        QCOMPARE(rig.client->status(), Status::Searching);
        QTest::qWait(300);
        QVERIFY(rig.server.messages().isEmpty());
        QCOMPARE(rig.server.clientCount(), 0);

        rig.secrets.load();
        QVERIFY(rig.waitFor(Status::Connected));
        QCOMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 0);
    }

    void notRunningThenBroadcastReconnects()
    {
        const quint16 apiPort = freePort();
        const quint16 discovery = freeUdpPort();
        FakeVtsServer server;
        SecretStore secrets{SecretStore::Backend::Memory};
        secrets.load();
        QTRY_VERIFY(secrets.isLoaded());
        VtsClient client(&secrets);
        QUrl url;
        url.setScheme(QStringLiteral("ws"));
        url.setHost(QStringLiteral("127.0.0.1"));
        url.setPort(apiPort);
        client.setUrlForTesting(url);
        client.setDiscoveryPortForTesting(discovery);
        client.setReconnectDelays(20000, 30000); // only the broadcast can bring it back quickly
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::NotRunning, 5000);

        broadcast(discovery, false, apiPort); // VTS runs, the API is off
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::ApiOff, 3000);

        QVERIFY(server.listen(apiPort));
        broadcast(discovery, true, apiPort); // switched on
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);
    }

    void injectsTheMouthWithKeepAlive()
    {
        Rig rig;
        rig.client->setFaceFound(true);
        QSignalSpy model(rig.client.get(), &VtsClient::modelChanged);
        QVERIFY(rig.connect());
        QTRY_VERIFY(rig.client->parameters().contains(QStringLiteral("VoiceA")));

        rig.client->setMouth(0.72f, 0.2f, vowels(0.72f));
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("InjectParameterDataRequest")), 1, 2000);
        auto injects = rig.server.messages(QStringLiteral("InjectParameterDataRequest"));
        const FakeVtsServer::Message &first = injects.first();
        QCOMPARE(first.data.value(QStringLiteral("faceFound")).toBool(), true);
        QCOMPARE(first.data.value(QStringLiteral("mode")).toString(), QStringLiteral("set"));
        const QVariantMap v = FakeVtsServer::injected(first);
        QCOMPARE(v.value(QStringLiteral("MouthOpen")).toDouble(), 0.72);
        QCOMPARE(v.value(QStringLiteral("MouthSmile")).toDouble(), 0.6); // (0.2 + 1) / 2
        QCOMPARE(v.value(QStringLiteral("VoiceA")).toDouble(), 0.72);
        QCOMPARE(v.value(QStringLiteral("VoiceO")).toDouble(), 0.0);
        QVERIFY(!v.contains(QStringLiteral("VocalInkVolume")));

        // Unchanged (or changed by less than 0.01): not re-sent right away.
        for (int i = 0; i < 6; ++i) {
            rig.client->setMouth(0.725f, 0.2f, vowels(0.72f));
            QTest::qWait(30);
        }
        QCOMPARE(rig.server.count(QStringLiteral("InjectParameterDataRequest")), 1);
        rig.client->setMouth(0.4f, 0.2f, vowels(0.4f));
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("InjectParameterDataRequest")), 2, 1000);

        // Held without new values: re-sent at least once a second.
        QTest::qWait(2 * VtsClient::kKeepAliveMs + 300);
        injects = rig.server.messages(QStringLiteral("InjectParameterDataRequest"));
        QVERIFY2(injects.size() >= 4, qPrintable(QString::number(injects.size())));
        for (qsizetype i = 2; i < injects.size(); ++i) {
            const qint64 gap = injects.at(i).atMs - injects.at(i - 1).atMs;
            QVERIFY2(gap <= 1000, qPrintable(QString::number(gap)));
        }
        QCOMPARE(FakeVtsServer::injected(injects.last()).value(QStringLiteral("MouthOpen")).toDouble(), 0.4);

        // release(): one closing frame, then nothing (VTS hands the mouth back).
        const int before = rig.server.count(QStringLiteral("InjectParameterDataRequest"));
        rig.client->release();
        QVERIFY(!rig.client->isInControl());
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("InjectParameterDataRequest")), before + 1, 1000);
        const QVariantMap closed = FakeVtsServer::injected(rig.server.messages(QStringLiteral("InjectParameterDataRequest")).last());
        QCOMPARE(closed.value(QStringLiteral("MouthOpen")).toDouble(), 0.0);
        QCOMPARE(closed.value(QStringLiteral("MouthSmile")).toDouble(), 0.5);
        QTest::qWait(VtsClient::kKeepAliveMs + 300);
        QCOMPARE(rig.server.count(QStringLiteral("InjectParameterDataRequest")), before + 1);
    }

    void customParametersAndUnknownIds()
    {
        Rig rig;
        rig.client->setCustomParameters(true);
        QVERIFY(rig.connect());
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("ParameterCreationRequest")), 2, 2000);
        for (const auto &m : rig.server.messages(QStringLiteral("ParameterCreationRequest"))) {
            QVERIFY(m.data.value(QStringLiteral("parameterName")).toString().startsWith(QStringLiteral("VocalInk")));
            QCOMPARE(m.data.value(QStringLiteral("min")).toDouble(), 0.0);
            QCOMPARE(m.data.value(QStringLiteral("max")).toDouble(), 1.0);
            QCOMPARE(m.data.value(QStringLiteral("defaultValue")).toDouble(), 0.0);
        }
        QTest::qWait(100);
        rig.client->setTalking(true);
        rig.client->setMouth(0.5f, 0.0f, vowels());
        QTRY_VERIFY_WITH_TIMEOUT(rig.server.count(QStringLiteral("InjectParameterDataRequest")) >= 1, 1000);
        const QVariantMap v = FakeVtsServer::injected(rig.server.messages(QStringLiteral("InjectParameterDataRequest")).last());
        QCOMPARE(v.value(QStringLiteral("VocalInkVolume")).toDouble(), 0.5);
        QCOMPARE(v.value(QStringLiteral("VocalInkSpeaking")).toDouble(), 1.0);

        // A parameter this model doesn't have: shown in detail, still connected.
        rig.client->setMouthParameters(QStringLiteral("NoSuchParam"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(rig.client->detail().contains(QStringLiteral("453")), 2000);
        QCOMPARE(rig.client->status(), Status::Connected);
        rig.client->setMouthParameters(QStringLiteral("MouthOpen"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(rig.client->detail().isEmpty(), 2000);
        rig.client->release();
    }

    void hotkeysAndExpressions()
    {
        Rig rig;
        rig.server.hotkeys = {hotkey(QStringLiteral("hk1"), QStringLiteral("Wave")),
                              hotkey(QStringLiteral("hk2"), QStringLiteral("Nod"))};
        rig.server.expressions = {QJsonObject{{QStringLiteral("name"), QStringLiteral("Smile")},
                                              {QStringLiteral("file"), QStringLiteral("smile.exp3.json")},
                                              {QStringLiteral("active"), false}}};
        QVERIFY(rig.connect());
        QTRY_COMPARE_WITH_TIMEOUT(rig.client->hotkeys().size(), 2, 2000);
        const QVariantMap wave = rig.client->hotkeys().first().toMap();
        QCOMPARE(wave.value(QStringLiteral("id")).toString(), QStringLiteral("hk1"));
        QCOMPARE(wave.value(QStringLiteral("name")).toString(), QStringLiteral("Wave"));
        QCOMPARE(wave.value(QStringLiteral("type")).toString(), QStringLiteral("TriggerAnimation"));
        QTRY_COMPARE(rig.client->expressions().size(), 1);
        QCOMPARE(rig.client->expressions().first().toMap().value(QStringLiteral("file")).toString(),
                 QStringLiteral("smile.exp3.json"));
        QTRY_COMPARE(rig.client->modelName(), QStringLiteral("Test Model"));

        rig.client->triggerHotkey(QStringLiteral("hk2"));
        rig.client->triggerHotkey(QString()); // nothing
        QTRY_COMPARE(rig.server.count(QStringLiteral("HotkeyTriggerRequest")), 1);
        QCOMPARE(rig.server.messages(QStringLiteral("HotkeyTriggerRequest")).first().data.value(QStringLiteral("hotkeyID")).toString(),
                 QStringLiteral("hk2"));

        rig.client->setExpression(QStringLiteral("smile.exp3.json"), true);
        QTRY_COMPARE(rig.server.count(QStringLiteral("ExpressionActivationRequest")), 1);
        const QJsonObject e = rig.server.messages(QStringLiteral("ExpressionActivationRequest")).first().data;
        QCOMPARE(e.value(QStringLiteral("expressionFile")).toString(), QStringLiteral("smile.exp3.json"));
        QCOMPARE(e.value(QStringLiteral("active")).toBool(), true);
        QCOMPARE(e.value(QStringLiteral("fadeTime")).toDouble(), 0.25);
    }

    void modelLoadedEventRefreshes()
    {
        Rig rig;
        rig.server.hotkeys = {hotkey(QStringLiteral("hk1"), QStringLiteral("Wave"))};
        QVERIFY(rig.connect());
        QTRY_COMPARE_WITH_TIMEOUT(rig.server.count(QStringLiteral("EventSubscriptionRequest")), 1, 2000);
        const QJsonObject sub = rig.server.messages(QStringLiteral("EventSubscriptionRequest")).first().data;
        QCOMPARE(sub.value(QStringLiteral("eventName")).toString(), QStringLiteral("ModelLoadedEvent"));
        QCOMPARE(sub.value(QStringLiteral("subscribe")).toBool(), true);
        QTRY_COMPARE(rig.client->hotkeys().size(), 1);

        rig.server.modelName = QStringLiteral("Other Model");
        rig.server.hotkeys = {hotkey(QStringLiteral("hk7"), QStringLiteral("Jump")),
                              hotkey(QStringLiteral("hk8"), QStringLiteral("Spin"))};
        rig.server.sendEvent(QStringLiteral("ModelLoadedEvent"),
                             {{QStringLiteral("modelLoaded"), true},
                              {QStringLiteral("modelName"), QStringLiteral("Other Model")},
                              {QStringLiteral("modelID"), QStringLiteral("m2")}});
        QTRY_COMPARE_WITH_TIMEOUT(rig.client->hotkeys().size(), 2, 2000);
        QCOMPARE(rig.client->modelName(), QStringLiteral("Other Model"));
        QCOMPARE(rig.server.count(QStringLiteral("HotkeysInCurrentModelRequest")), 2);
        QTRY_COMPARE(rig.server.count(QStringLiteral("InputParameterListRequest")), 2);
    }

    void disabledClosesEverything()
    {
        Rig rig;
        QVERIFY(rig.connect());
        rig.client->setMouth(0.5f, 0.0f, vowels());
        rig.client->setEnabled(false);
        QCOMPARE(rig.client->status(), Status::Off);
        QTRY_COMPARE(rig.server.clientCount(), 0);
        const int injects = rig.server.count(QStringLiteral("InjectParameterDataRequest"));
        QTest::qWait(VtsClient::kKeepAliveMs + 100);
        QCOMPARE(rig.server.count(QStringLiteral("InjectParameterDataRequest")), injects);
    }

    void pluginIconMustBe128Square()
    {
        Rig rig;
        QVERIFY(!png(64).isEmpty());
        rig.client->setPluginIcon(png(64));
        rig.client->setPluginIcon(QByteArray("not a png"));
        rig.client->setEnabled(true);
        QTRY_COMPARE(rig.server.count(QStringLiteral("AuthenticationTokenRequest")), 1);
        QVERIFY(!rig.server.messages(QStringLiteral("AuthenticationTokenRequest")).first().data.contains(QStringLiteral("pluginIcon")));
    }
};

QTEST_GUILESS_MAIN(TestVts)
#include "test_vts.moc"
