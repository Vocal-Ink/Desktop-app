#include "obs/ObsIntegration.h"
#include "obs/ObsWebSocketClient.h"
#include "support/FakeObsServer.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>
#include <QWebSocketServer>

namespace {

using Response = FakeObsServer::Response;

ObsIntegration::Config configFor(const FakeObsServer &server)
{
    ObsIntegration::Config c;
    c.enabled = true;
    c.host = QStringLiteral("127.0.0.1");
    c.port = server.port();
    return c;
}

// Fast retries and timeouts so failures show up quickly.
void speedUp(ObsIntegration &obs)
{
    obs.client()->setReconnectDelays(50, 200);
    obs.client()->setRequestTimeout(2000);
}

bool waitForStatus(const ObsIntegration &obs, ObsIntegration::Status status, int timeoutMs = 5000)
{
    return QTest::qWaitFor([&] { return obs.status() == status; }, timeoutMs);
}

QString textOf(const FakeObsServer::Request &r)
{
    return r.data.value(QStringLiteral("inputSettings")).toObject().value(QStringLiteral("text")).toString();
}

struct Done
{
    int calls = 0;
    bool ok = false;
    QString message;
    ObsIntegration::DoneCallback callback()
    {
        return [this](bool success, const QString &text) {
            ++calls;
            ok = success;
            message = text;
        };
    }
};

struct Listed
{
    int calls = 0;
    QStringList names;
    QString error;
    ObsIntegration::ListCallback callback()
    {
        return [this](const QStringList &list, const QString &err) {
            ++calls;
            names = list;
            error = err;
        };
    }
};

#ifdef Q_OS_WIN
const QString kExpectedTextKind = QStringLiteral("text_gdiplus_v3");
#else
const QString kExpectedTextKind = QStringLiteral("text_ft2_source_v2");
#endif

} // namespace

class TestObs : public QObject
{
    Q_OBJECT
private slots:
    void authStringMatchesReferenceVectors()
    {
        // Computed with Python's hashlib/base64 (the first is the example from the obs-websocket docs).
        QCOMPARE(ObsWebSocketClient::authString(QStringLiteral("supersecretpassword"),
                                                QStringLiteral("lM1GncleQOaCu9lT1yeUZhFYnqhsLLP1G5lAGo3ixaI="),
                                                QStringLiteral("+IxH4CnCiqpX1rM9scsNynZzbOe4KhDeYcTNS3PDaeY=")),
                 QStringLiteral("1Ct943GAT+6YQUUX47Ia/ncufilbe6+oD6lY+5kaCu4="));
        QCOMPARE(ObsWebSocketClient::authString(QString::fromUtf8("p\xc3\xa4ssw\xc3\xb6rd"), QStringLiteral("salt123"),
                                                QStringLiteral("challenge456")),
                 QStringLiteral("LG1p0brHiN6hmcmYisBfWOq/8PQsd/lfMdbdtoGpt/M="));
        QCOMPARE(ObsWebSocketClient::authString(QString(), QStringLiteral("abc"), QStringLiteral("def")),
                 QStringLiteral("Bgd1skVrUD3eg/l0wQM6+i829tg60hPywyKoDpgGQ4Y="));
    }

    void identifiesWithoutPassword()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        QSignalSpy identified(obs.client(), &ObsWebSocketClient::identified);
        obs.setConfig(configFor(server));
        QCOMPARE(obs.status(), ObsIntegration::Status::Connecting);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        QCOMPARE(obs.statusText(), QStringLiteral("Connected to OBS 31.0.2"));
        QCOMPARE(identified.size(), 1);
        QCOMPARE(identified.first().at(0).toString(), QStringLiteral("31.0.2"));
        QCOMPARE(obs.client()->state(), ObsWebSocketClient::State::Identified);

        const QJsonObject identify = server.lastIdentify();
        QCOMPARE(identify.value(QStringLiteral("rpcVersion")).toInt(), 1);
        QCOMPARE(identify.value(QStringLiteral("eventSubscriptions")).toInt(-1), 0);
        QVERIFY(!identify.contains(QStringLiteral("authentication")));
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        QCOMPARE(server.lastSubprotocol(), QStringLiteral("obswebsocket.json"));
#endif
    }

    void identifiesWithPassword()
    {
        FakeObsServer server;
        server.setPassword(QStringLiteral("hunter2"));
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.password = QStringLiteral("hunter2");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        QCOMPARE(server.identifiedCount(), 1);
        QCOMPARE(server.authFailures(), 0);
        QVERIFY(!server.lastIdentify().value(QStringLiteral("authentication")).toString().isEmpty());
    }

    void wrongPasswordFailsWithoutRetrying()
    {
        FakeObsServer server;
        server.setPassword(QStringLiteral("right"));
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        QSignalSpy authFailed(obs.client(), &ObsWebSocketClient::authenticationFailed);
        QSignalSpy statuses(&obs, &ObsIntegration::statusChanged);
        ObsIntegration::Config c = configFor(server);
        c.password = QStringLiteral("wrong");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::AuthFailed));
        QCOMPARE(obs.statusText(), QStringLiteral("Wrong OBS WebSocket password"));
        QCOMPARE(authFailed.size(), 1);
        QTest::qWait(400);
        QCOMPARE(server.helloCount(), 1); // no retry loop with a bad password
        QCOMPARE(obs.status(), ObsIntegration::Status::AuthFailed);

        // Fixing the password reconnects.
        c.password = QStringLiteral("right");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        int authStatuses = 0;
        for (const auto &args : statuses) {
            if (args.at(0).value<ObsIntegration::Status>() == ObsIntegration::Status::AuthFailed)
                ++authStatuses;
        }
        QCOMPARE(authStatuses, 1);
    }

    void missingPasswordAsksForIt()
    {
        FakeObsServer server;
        server.setPassword(QStringLiteral("secret"));
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::AuthFailed));
        QVERIFY(obs.statusText().contains(QLatin1String("password")));
    }

    void obsNotRunningIsReportedOnce()
    {
        quint16 port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            port = probe.serverPort();
        }
        ObsIntegration obs;
        speedUp(obs);
        QSignalSpy statuses(&obs, &ObsIntegration::statusChanged);
        ObsIntegration::Config c;
        c.enabled = true;
        c.host = QStringLiteral("127.0.0.1");
        c.port = port;
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Error));
        QCOMPARE(obs.statusText(),
                 QStringLiteral("OBS is not running or the WebSocket server is off (Tools → WebSocket Server Settings)"));
        QTest::qWait(600); // several retries
        int errors = 0;
        for (const auto &args : statuses) {
            if (args.at(0).value<ObsIntegration::Status>() == ObsIntegration::Status::Error)
                ++errors;
        }
        QCOMPARE(errors, 1); // retries don't spam status changes (and notifications)

        // OBS starts later: the retry loop picks it up.
        FakeObsServer server;
        QVERIFY(server.listen());
        c.port = server.port();
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
    }

    void silentServerTimesOut()
    {
        // A WebSocket server that never says Hello (e.g. the old obs-websocket 4.x).
        QWebSocketServer silent(QStringLiteral("old"), QWebSocketServer::NonSecureMode);
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        ObsWebSocketClient client;
        client.setHandshakeTimeout(600);
        client.setReconnectDelays(5000, 5000);
        QSignalSpy errors(&client, &ObsWebSocketClient::errorOccurred);
        client.connectTo(QStringLiteral("127.0.0.1"), silent.serverPort(), QString());
        QCOMPARE(client.state(), ObsWebSocketClient::State::Connecting);
        QVERIFY(errors.wait(3000));
        QVERIFY(errors.first().at(0).toString().contains(QLatin1String("OBS Studio 28")));
        QCOMPARE(client.state(), ObsWebSocketClient::State::Disconnected);
        client.disconnectFrom();
    }

    void requestsAreCorrelated()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("Slow"), Response::success({{QStringLiteral("who"), QStringLiteral("slow")}}, 150));
        server.setResponse(QStringLiteral("Fast"), Response::success({{QStringLiteral("who"), QStringLiteral("fast")}}));
        server.setResponse(QStringLiteral("Broken"), Response::failure(600, QStringLiteral("No source was found")));
        ObsWebSocketClient client;
        client.connectTo(QStringLiteral("127.0.0.1"), server.port(), QString());
        QTRY_COMPARE(client.state(), ObsWebSocketClient::State::Identified);

        QStringList order;
        client.request(QStringLiteral("Slow"), {{QStringLiteral("n"), 1}}, [&](bool ok, const QJsonObject &data, const QString &) {
            QVERIFY(ok);
            order << QStringLiteral("Slow:") + data.value(QStringLiteral("who")).toString();
        });
        client.request(QStringLiteral("Fast"), {}, [&](bool ok, const QJsonObject &data, const QString &) {
            QVERIFY(ok);
            order << QStringLiteral("Fast:") + data.value(QStringLiteral("who")).toString();
        });
        client.request(QStringLiteral("Broken"), {}, [&](bool ok, const QJsonObject &status, const QString &error) {
            QVERIFY(!ok);
            QCOMPARE(status.value(QStringLiteral("code")).toInt(), 600);
            order << QStringLiteral("Broken:") + error;
        });
        QTRY_COMPARE(order.size(), 3);
        QCOMPARE(order, (QStringList{QStringLiteral("Fast:fast"), QStringLiteral("Broken:No source was found"),
                                     QStringLiteral("Slow:slow")}));

        const QList<FakeObsServer::Request> slow = server.requests(QStringLiteral("Slow"));
        QCOMPARE(slow.size(), 1);
        QCOMPARE(slow.first().data.value(QStringLiteral("n")).toInt(), 1);
        QVERIFY(!slow.first().id.isEmpty());
        QVERIFY(server.requests(QStringLiteral("Fast")).first().id != slow.first().id);
    }

    void requestsTimeOut()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("Never"), Response::noAnswer());
        ObsWebSocketClient client;
        client.setRequestTimeout(150);
        client.connectTo(QStringLiteral("127.0.0.1"), server.port(), QString());
        QTRY_COMPARE(client.state(), ObsWebSocketClient::State::Identified);

        int calls = 0;
        QString error;
        QElapsedTimer elapsed;
        elapsed.start();
        client.request(QStringLiteral("Never"), {}, [&](bool ok, const QJsonObject &, const QString &e) {
            QVERIFY(!ok);
            ++calls;
            error = e;
        });
        QTRY_COMPARE_WITH_TIMEOUT(calls, 1, 2000);
        QVERIFY(elapsed.elapsed() >= 100);
        QVERIFY(error.contains(QLatin1String("Never")));

        // The connection is still usable afterwards.
        bool answered = false;
        client.request(QStringLiteral("GetVersion"), {}, [&](bool ok, const QJsonObject &, const QString &) { answered = ok; });
        QTRY_VERIFY(answered);
        QTest::qWait(200);
        QCOMPARE(calls, 1);
    }

    void pendingRequestsFailOnDisconnectAndClientReconnects()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("Never"), Response::noAnswer());
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        QSignalSpy statuses(&obs, &ObsIntegration::statusChanged);

        int calls = 0;
        bool ok = true;
        obs.client()->request(QStringLiteral("Never"), {}, [&](bool success, const QJsonObject &, const QString &) {
            ++calls;
            ok = success;
        });
        QTRY_COMPARE(server.requests(QStringLiteral("Never")).size(), 1);
        server.disconnectClients(1001, QStringLiteral("OBS is shutting down"));
        QTRY_COMPARE(calls, 1);
        QVERIFY(!ok);

        QTRY_COMPARE(server.helloCount(), 2);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        QTest::qWait(100);
        QCOMPARE(calls, 1);
        QVERIFY(!statuses.isEmpty());
        QCOMPARE(statuses.first().at(0).value<ObsIntegration::Status>(), ObsIntegration::Status::Connecting);
    }

    void onlyConnectionSettingsReconnect()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        c.subtitles = true;
        c.subtitleSource = QStringLiteral("Subs");
        c.clearAfterMs = 1234;
        c.captions = true;
        obs.setConfig(c);
        QTest::qWait(200);
        QCOMPARE(server.helloCount(), 1);
        QCOMPARE(obs.status(), ObsIntegration::Status::Connected);

        c.password = QStringLiteral("changed"); // the fake accepts anything without auth
        obs.setConfig(c);
        QTRY_COMPARE(server.helloCount(), 2);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        c.enabled = false;
        obs.setConfig(c);
        QCOMPARE(obs.status(), ObsIntegration::Status::Disabled);
        QCOMPARE(obs.client()->state(), ObsWebSocketClient::State::Disconnected);
        QTRY_COMPARE(server.clientCount(), 0);
        QTest::qWait(200);
        QCOMPARE(server.helloCount(), 2);
    }

    void subtitlesAreSetAndCleared()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.subtitles = true;
        c.subtitleSource = QStringLiteral("Subs");
        c.clearAfterMs = 150;
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        obs.utteranceStarted(QStringLiteral("Hello world"));
        QTRY_COMPARE(server.requests(QStringLiteral("SetInputSettings")).size(), 1);
        const FakeObsServer::Request set = server.requests(QStringLiteral("SetInputSettings")).first();
        QCOMPARE(set.data.value(QStringLiteral("inputName")).toString(), QStringLiteral("Subs"));
        QCOMPARE(textOf(set), QStringLiteral("Hello world"));
        QCOMPARE(set.data.value(QStringLiteral("overlay")).toBool(), true);

        QElapsedTimer elapsed;
        elapsed.start();
        obs.utteranceFinished(QStringLiteral("Hello world"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(QStringLiteral("SetInputSettings")).size(), 2, 2000);
        QVERIFY(elapsed.elapsed() >= 100);
        QCOMPARE(textOf(server.requests(QStringLiteral("SetInputSettings")).at(1)), QString());
        QCOMPARE(obs.statusText(), QStringLiteral("Connected to OBS 31.0.2"));
    }

    void nextUtteranceCancelsTheClear()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.subtitles = true;
        c.subtitleSource = QStringLiteral("Subs");
        c.clearAfterMs = 250;
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        obs.utteranceStarted(QStringLiteral("One"));
        obs.utteranceFinished(QStringLiteral("One"));
        obs.utteranceStarted(QStringLiteral("Two"));
        QTest::qWait(400);
        QStringList texts;
        for (const auto &r : server.requests(QStringLiteral("SetInputSettings")))
            texts << textOf(r);
        QCOMPARE(texts, (QStringList{QStringLiteral("One"), QStringLiteral("Two")}));

        // clearAfterMs = 0 keeps the last line.
        c.clearAfterMs = 0;
        obs.setConfig(c);
        obs.utteranceFinished(QStringLiteral("Two"));
        QTest::qWait(300);
        QCOMPARE(server.requests(QStringLiteral("SetInputSettings")).size(), 2);
    }

    void subtitleErrorsShowInTheStatus()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.queueResponse(QStringLiteral("SetInputSettings"),
                             Response::failure(600, QStringLiteral("No source was found by the name of `Subs`.")));
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.subtitles = true;
        c.subtitleSource = QStringLiteral("Subs");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        obs.utteranceStarted(QStringLiteral("Hi"));
        QTRY_VERIFY(obs.statusText().contains(QLatin1String("No source was found")));
        QCOMPARE(obs.status(), ObsIntegration::Status::Connected);
        obs.utteranceStarted(QStringLiteral("Hi again")); // works now
        QTRY_COMPARE(obs.statusText(), QStringLiteral("Connected to OBS 31.0.2"));
    }

    void captionsAreSentAndFailuresIgnored()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("SendStreamCaption"), Response::failure(501, QStringLiteral("Streaming is not active.")));
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.captions = true;
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
        QSignalSpy statuses(&obs, &ObsIntegration::statusChanged);

        obs.utteranceStarted(QStringLiteral("Can everyone hear me?"));
        QTRY_COMPARE(server.requests(QStringLiteral("SendStreamCaption")).size(), 1);
        QCOMPARE(server.requests(QStringLiteral("SendStreamCaption")).first().data.value(QStringLiteral("captionText")).toString(),
                 QStringLiteral("Can everyone hear me?"));
        obs.utteranceFinished(QStringLiteral("Can everyone hear me?"));
        QTest::qWait(100);
        QVERIFY(statuses.isEmpty());
        QCOMPARE(server.requestTypes(), QStringList{QStringLiteral("SendStreamCaption")});
    }

    void indicatorIsShownAndHidden()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"),
                           Response::success({{QStringLiteral("sceneName"), QStringLiteral("Live")},
                                              {QStringLiteral("currentProgramSceneName"), QStringLiteral("Live")}}));
        server.setResponse(QStringLiteral("GetSceneItemId"), Response::success({{QStringLiteral("sceneItemId"), 12}}));
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.indicator = true;
        c.indicatorSource = QStringLiteral("Avatar talking");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        obs.utteranceStarted(QStringLiteral("Hi"));
        QTRY_COMPARE(server.requests(QStringLiteral("SetSceneItemEnabled")).size(), 1);
        const FakeObsServer::Request lookup = server.requests(QStringLiteral("GetSceneItemId")).first();
        QCOMPARE(lookup.data.value(QStringLiteral("sceneName")).toString(), QStringLiteral("Live"));
        QCOMPARE(lookup.data.value(QStringLiteral("sourceName")).toString(), QStringLiteral("Avatar talking"));
        const FakeObsServer::Request show = server.requests(QStringLiteral("SetSceneItemEnabled")).first();
        QCOMPARE(show.data.value(QStringLiteral("sceneName")).toString(), QStringLiteral("Live"));
        QCOMPARE(show.data.value(QStringLiteral("sceneItemId")).toInt(), 12);
        QCOMPARE(show.data.value(QStringLiteral("sceneItemEnabled")).toBool(), true);

        obs.utteranceFinished(QStringLiteral("Hi"));
        QTRY_COMPARE(server.requests(QStringLiteral("SetSceneItemEnabled")).size(), 2);
        const FakeObsServer::Request hide = server.requests(QStringLiteral("SetSceneItemEnabled")).at(1);
        QCOMPARE(hide.data.value(QStringLiteral("sceneName")).toString(), QStringLiteral("Live"));
        QCOMPARE(hide.data.value(QStringLiteral("sceneItemId")).toInt(), 12);
        QCOMPARE(hide.data.value(QStringLiteral("sceneItemEnabled")).toBool(), false);
        QCOMPARE(server.requestTypes(),
                 (QStringList{QStringLiteral("GetCurrentProgramScene"), QStringLiteral("GetSceneItemId"),
                              QStringLiteral("SetSceneItemEnabled"), QStringLiteral("SetSceneItemEnabled")}));
    }

    void indicatorStaysHiddenWhenSpeechEndsDuringLookup()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"),
                           Response::success({{QStringLiteral("currentProgramSceneName"), QStringLiteral("Old OBS scene")}}));
        server.setResponse(QStringLiteral("GetSceneItemId"), Response::success({{QStringLiteral("sceneItemId"), 3}}, 400));
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.indicator = true;
        c.indicatorSource = QStringLiteral("Avatar");
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        obs.utteranceStarted(QStringLiteral("Quick"));
        QTRY_COMPARE(server.requests(QStringLiteral("GetSceneItemId")).size(), 1);
        QCOMPARE(server.requests(QStringLiteral("GetSceneItemId")).first().data.value(QStringLiteral("sceneName")).toString(),
                 QStringLiteral("Old OBS scene"));
        obs.utteranceFinished(QStringLiteral("Quick"));
        QTest::qWait(700);
        QVERIFY(server.requests(QStringLiteral("SetSceneItemEnabled")).isEmpty());
    }

    void listTextSourcesFiltersByKind()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        const QJsonArray inputs{
            QJsonObject{{QStringLiteral("inputName"), QStringLiteral("subtitles")},
                        {QStringLiteral("inputKind"), QStringLiteral("text_gdiplus_v3")},
                        {QStringLiteral("unversionedInputKind"), QStringLiteral("text_gdiplus")}},
            QJsonObject{{QStringLiteral("inputName"), QStringLiteral("Webcam")},
                        {QStringLiteral("inputKind"), QStringLiteral("dshow_input")},
                        {QStringLiteral("unversionedInputKind"), QStringLiteral("dshow_input")}},
            QJsonObject{{QStringLiteral("inputName"), QStringLiteral("Lower third")},
                        {QStringLiteral("inputKind"), QStringLiteral("text_ft2_source_v2")},
                        {QStringLiteral("unversionedInputKind"), QStringLiteral("text_ft2_source")}},
            QJsonObject{{QStringLiteral("inputName"), QStringLiteral("Avatar")},
                        {QStringLiteral("inputKind"), QStringLiteral("image_source")},
                        {QStringLiteral("unversionedInputKind"), QStringLiteral("image_source")}},
            QJsonObject{{QStringLiteral("inputName"), QStringLiteral("Chat box")},
                        {QStringLiteral("inputKind"), QStringLiteral("text_ft2_source_v2")}},
        };
        server.setResponse(QStringLiteral("GetInputList"), Response::success({{QStringLiteral("inputs"), inputs}}));
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        Listed text;
        obs.listTextSources(text.callback());
        QTRY_COMPARE(text.calls, 1);
        QVERIFY(text.error.isEmpty());
        QCOMPARE(text.names, (QStringList{QStringLiteral("Chat box"), QStringLiteral("Lower third"), QStringLiteral("subtitles")}));

        Listed all;
        obs.listAllSources(all.callback());
        QTRY_COMPARE(all.calls, 1);
        QCOMPARE(all.names, (QStringList{QStringLiteral("Avatar"), QStringLiteral("Chat box"), QStringLiteral("Lower third"),
                                         QStringLiteral("subtitles"), QStringLiteral("Webcam")}));
        QTest::qWait(50);
        QCOMPARE(text.calls, 1);
        QCOMPARE(all.calls, 1);
    }

    void createTextSourceSendsTheRightRequest()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"), Response::success({{QStringLiteral("sceneName"), QStringLiteral("Gaming")}}));
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        Done done;
        obs.createTextSource(QStringLiteral("  Vocal Ink subtitles "), done.callback());
        QTRY_COMPARE(done.calls, 1);
        QVERIFY2(done.ok, qPrintable(done.message));
        QVERIFY(done.message.contains(QLatin1String("Vocal Ink subtitles")));

        const QList<FakeObsServer::Request> created = server.requests(QStringLiteral("CreateInput"));
        QCOMPARE(created.size(), 1);
        const QJsonObject d = created.first().data;
        QCOMPARE(d.value(QStringLiteral("sceneName")).toString(), QStringLiteral("Gaming"));
        QCOMPARE(d.value(QStringLiteral("inputName")).toString(), QStringLiteral("Vocal Ink subtitles"));
        QCOMPARE(d.value(QStringLiteral("inputKind")).toString(), kExpectedTextKind);
        QCOMPARE(d.value(QStringLiteral("sceneItemEnabled")).toBool(), true);
        const QJsonObject settings = d.value(QStringLiteral("inputSettings")).toObject();
        QVERIFY(settings.contains(QStringLiteral("text")));
        QCOMPARE(settings.value(QStringLiteral("text")).toString(), QString());
        const QJsonObject font = settings.value(QStringLiteral("font")).toObject();
        QCOMPARE(font.value(QStringLiteral("face")).toString(), QStringLiteral("Arial"));
        QCOMPARE(font.value(QStringLiteral("size")).toInt(), 48);

        Done empty;
        obs.createTextSource(QStringLiteral("   "), empty.callback());
        QCOMPARE(empty.calls, 0); // always asynchronous
        QTRY_COMPARE(empty.calls, 1);
        QVERIFY(!empty.ok);
    }

    void createTextSourceFallsBackToAnAvailableKind()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"), Response::success({{QStringLiteral("sceneName"), QStringLiteral("Gaming")}}));
        server.queueResponse(QStringLiteral("CreateInput"),
                             Response::failure(605, QStringLiteral("Your specified input kind is not supported by OBS.")));
        const QJsonArray kinds{QStringLiteral("image_source"), QStringLiteral("text_gdiplus_v2"),
                               QStringLiteral("text_ft2_source"), QStringLiteral("browser_source")};
        server.setResponse(QStringLiteral("GetInputKindList"), Response::success({{QStringLiteral("inputKinds"), kinds}}));
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        Done done;
        obs.createTextSource(QStringLiteral("Subs"), done.callback());
        QTRY_COMPARE(done.calls, 1);
        QVERIFY2(done.ok, qPrintable(done.message));
        const QList<FakeObsServer::Request> created = server.requests(QStringLiteral("CreateInput"));
        QCOMPARE(created.size(), 2);
        QCOMPARE(created.at(0).data.value(QStringLiteral("inputKind")).toString(), kExpectedTextKind);
#ifdef Q_OS_WIN
        QCOMPARE(created.at(1).data.value(QStringLiteral("inputKind")).toString(), QStringLiteral("text_gdiplus_v2"));
#else
        QCOMPARE(created.at(1).data.value(QStringLiteral("inputKind")).toString(), QStringLiteral("text_ft2_source"));
#endif
        QCOMPARE(created.at(1).data.value(QStringLiteral("inputName")).toString(), QStringLiteral("Subs"));

        // Other failures are reported as they are.
        server.queueResponse(QStringLiteral("CreateInput"),
                             Response::failure(601, QStringLiteral("An input already exists by that input name.")));
        Done taken;
        obs.createTextSource(QStringLiteral("Subs"), taken.callback());
        QTRY_COMPARE(taken.calls, 1);
        QVERIFY(!taken.ok);
        QCOMPARE(taken.message, QStringLiteral("An input already exists by that input name."));
    }

    void addBrowserOverlaySendsTheRightRequest()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"), Response::success({{QStringLiteral("sceneName"), QStringLiteral("Just chatting")}}));
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        const QUrl url(QStringLiteral("http://127.0.0.1:7342/?style=bubble&name=1"));
        Done done;
        obs.addBrowserOverlay(QStringLiteral("Vocal Ink captions"), url, done.callback());
        QTRY_COMPARE(done.calls, 1);
        QVERIFY2(done.ok, qPrintable(done.message));
        const QList<FakeObsServer::Request> created = server.requests(QStringLiteral("CreateInput"));
        QCOMPARE(created.size(), 1);
        const QJsonObject d = created.first().data;
        QCOMPARE(d.value(QStringLiteral("sceneName")).toString(), QStringLiteral("Just chatting"));
        QCOMPARE(d.value(QStringLiteral("inputName")).toString(), QStringLiteral("Vocal Ink captions"));
        QCOMPARE(d.value(QStringLiteral("inputKind")).toString(), QStringLiteral("browser_source"));
        QCOMPARE(d.value(QStringLiteral("sceneItemEnabled")).toBool(), true);
        const QJsonObject settings = d.value(QStringLiteral("inputSettings")).toObject();
        QCOMPARE(settings.value(QStringLiteral("url")).toString(), url.toString());
        QCOMPARE(settings.value(QStringLiteral("width")).toInt(), 1920);
        QCOMPARE(settings.value(QStringLiteral("height")).toInt(), 1080);
        QVERIFY(settings.contains(QStringLiteral("css")));
        QCOMPARE(settings.value(QStringLiteral("css")).toString(), QString());
    }

    void addBrowserOverlayUpdatesAnExistingOne()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetCurrentProgramScene"), Response::success({{QStringLiteral("sceneName"), QStringLiteral("Main")}}));
        server.setResponse(QStringLiteral("CreateInput"),
                           Response::failure(601, QStringLiteral("An input already exists by that input name.")));
        server.queueResponse(QStringLiteral("GetInputSettings"),
                             Response::success({{QStringLiteral("inputKind"), QStringLiteral("browser_source")}}));
        server.queueResponse(QStringLiteral("GetInputSettings"),
                             Response::success({{QStringLiteral("inputKind"), QStringLiteral("image_source")}}));
        ObsIntegration obs;
        speedUp(obs);
        obs.setConfig(configFor(server));
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        const QUrl url(QStringLiteral("http://127.0.0.1:9000/?style=plain"));
        Done done;
        obs.addBrowserOverlay(QStringLiteral("Captions"), url, done.callback());
        QTRY_COMPARE(done.calls, 1);
        QVERIFY2(done.ok, qPrintable(done.message));
        const QList<FakeObsServer::Request> updates = server.requests(QStringLiteral("SetInputSettings"));
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().data.value(QStringLiteral("inputName")).toString(), QStringLiteral("Captions"));
        QCOMPARE(updates.first().data.value(QStringLiteral("inputSettings")).toObject().value(QStringLiteral("url")).toString(),
                 url.toString());

        // A different kind of source with that name is left alone.
        Done clash;
        obs.addBrowserOverlay(QStringLiteral("Captions"), url, clash.callback());
        QTRY_COMPARE(clash.calls, 1);
        QVERIFY(!clash.ok);
        QCOMPARE(server.requests(QStringLiteral("SetInputSettings")).size(), 1);
    }

    void testSubtitleShowsAndClears()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        ObsIntegration obs;
        speedUp(obs);
        ObsIntegration::Config c = configFor(server);
        c.subtitleSource = QStringLiteral("Subs");
        c.clearAfterMs = 150;
        obs.setConfig(c);
        QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));

        Done done;
        obs.testSubtitle(done.callback());
        QTRY_COMPARE(done.calls, 1);
        QVERIFY2(done.ok, qPrintable(done.message));
        QCOMPARE(textOf(server.requests(QStringLiteral("SetInputSettings")).first()), QStringLiteral("Vocal Ink test subtitle"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests(QStringLiteral("SetInputSettings")).size(), 2, 2000);
        QCOMPARE(textOf(server.requests(QStringLiteral("SetInputSettings")).at(1)), QString());
    }

    void callbacksRunOnceWhenNotConnected()
    {
        ObsIntegration obs;
        ObsIntegration::Config c;
        c.subtitleSource = QStringLiteral("Subs");
        obs.setConfig(c);
        QCOMPARE(obs.status(), ObsIntegration::Status::Disabled);

        Listed text, all;
        Done create, overlay, test;
        obs.listTextSources(text.callback());
        obs.listAllSources(all.callback());
        obs.createTextSource(QStringLiteral("Subs"), create.callback());
        obs.addBrowserOverlay(QStringLiteral("Captions"), QUrl(QStringLiteral("http://127.0.0.1:7342/")), overlay.callback());
        obs.testSubtitle(test.callback());
        QCOMPARE(text.calls + all.calls + create.calls + overlay.calls + test.calls, 0);
        QTest::qWait(100);
        for (const Listed *l : {&text, &all}) {
            QCOMPARE(l->calls, 1);
            QCOMPARE(l->error, QStringLiteral("Not connected to OBS"));
            QVERIFY(l->names.isEmpty());
        }
        for (const Done *d : {&create, &overlay, &test}) {
            QCOMPARE(d->calls, 1);
            QVERIFY(!d->ok);
            QCOMPARE(d->message, QStringLiteral("Not connected to OBS"));
        }

        // Speech events are ignored quietly.
        obs.utteranceStarted(QStringLiteral("hello"));
        obs.utteranceFinished(QStringLiteral("hello"));

        int raw = 0;
        obs.client()->request(QStringLiteral("GetVersion"), {}, [&](bool ok, const QJsonObject &, const QString &) {
            QVERIFY(!ok);
            ++raw;
        });
        QTRY_COMPARE(raw, 1);
    }

    void destroyingWithPendingRequestsIsSafe()
    {
        FakeObsServer server;
        QVERIFY(server.listen());
        server.setResponse(QStringLiteral("GetInputList"), Response::noAnswer());
        int calls = 0;
        {
            ObsIntegration obs;
            speedUp(obs);
            obs.setConfig(configFor(server));
            QVERIFY(waitForStatus(obs, ObsIntegration::Status::Connected));
            obs.listTextSources([&](const QStringList &, const QString &) { ++calls; });
            QTRY_COMPARE(server.requests(QStringLiteral("GetInputList")).size(), 1);
        }
        QTest::qWait(100);
        QCOMPARE(calls, 0);
        QTRY_COMPARE(server.clientCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestObs)
#include "test_obs.moc"
