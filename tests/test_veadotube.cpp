#include "avatar/VeadotubeClient.h"
#include "support/FakeVeadoServer.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <QWebSocket>

namespace {

using Status = VeadotubeClient::Status;

void writeInstance(const QString &dir, const QString &fileName, const QString &server, qint64 time,
                   const QString &name = QStringLiteral("veadotube mini"))
{
    QFile f(dir + QLatin1Char('/') + fileName);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QJsonObject o{{QStringLiteral("time"), time},
                        {QStringLiteral("name"), name},
                        {QStringLiteral("id"), fileName},
                        {QStringLiteral("version"), QStringLiteral("2.1")},
                        {QStringLiteral("language"), QStringLiteral("en")},
                        {QStringLiteral("server"), server}};
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

qint64 now()
{
    return QDateTime::currentSecsSinceEpoch();
}

QString closedServer()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const QString s = QStringLiteral("127.0.0.1:%1").arg(probe.serverPort());
    probe.close();
    return s;
}

const QString kList = QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list"}})");

} // namespace

class TestVeadotube : public QObject
{
    Q_OBJECT
private slots:
    void offByDefault()
    {
        VeadotubeClient client;
        QCOMPARE(client.status(), Status::Off);
        client.setTalking(true);
        client.setState(QStringLiteral("s1"));
        QVERIFY(client.findChildren<QWebSocket *>().isEmpty());
    }

    void picksTheNewestInstance()
    {
        QTemporaryDir dir;
        FakeVeadoServer older, newer;
        QVERIFY(older.listen());
        QVERIFY(newer.listen());
        writeInstance(dir.path(), QStringLiteral("mini-0000018f00000000-00001111"), older.server(), now() - 600);
        writeInstance(dir.path(), QStringLiteral("mini-0000018f00000001-00002222"), newer.server(), now(),
                      QStringLiteral("veadotube mini"));

        const QList<VeadotubeClient::Instance> found = VeadotubeClient::readInstances(dir.path());
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.first().server, newer.server());

        VeadotubeClient client;
        client.setInstancesDirForTesting(dir.path());
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);
        QCOMPARE(client.instanceName(), QStringLiteral("veadotube mini"));
        QCOMPARE(client.server(), newer.server());
        QCOMPARE(older.connectionUrls().size(), 0);
        QCOMPARE(newer.connectionUrls().size(), 1);
        const QUrlQuery query(newer.connectionUrls().first());
        QCOMPARE(query.queryItemValue(QStringLiteral("n"), QUrl::FullyDecoded), QStringLiteral("Vocal Ink"));
    }

    void fallsBackWhenTheNewestIsGone()
    {
        QTemporaryDir dir;
        FakeVeadoServer running;
        QVERIFY(running.listen());
        writeInstance(dir.path(), QStringLiteral("mini-0000018f00000001-00002222"), closedServer(), now());
        writeInstance(dir.path(), QStringLiteral("mini-0000018f00000000-00001111"), running.server(), now() - 3600);
        QFile junk(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not json");
        junk.close();

        VeadotubeClient client;
        client.setInstancesDirForTesting(dir.path());
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 5000);
        QCOMPARE(client.server(), running.server());
    }

    void pollsUntilAnInstanceAppears()
    {
        QTemporaryDir dir;
        VeadotubeClient client;
        client.setPollIntervalForTesting(100);
        client.setInstancesDirForTesting(dir.path());
        client.setEnabled(true);
        QCOMPARE(client.status(), Status::NotRunning);

        FakeVeadoServer server;
        QVERIFY(server.listen());
        writeInstance(dir.path(), QStringLiteral("mini-0000018f00000002-00003333"), server.server(), now());
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);

        // veadotube restarts: not running for a moment, then connected again.
        QList<Status> seen;
        connect(&client, &VeadotubeClient::statusChanged, this, [&] { seen << client.status(); });
        server.disconnectClients();
        QTRY_VERIFY_WITH_TIMEOUT(seen.contains(Status::NotRunning), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);
        QCOMPARE(server.connectionUrls().size(), 2);
    }

    void listsStates()
    {
        QTemporaryDir dir;
        FakeVeadoServer server;
        QVERIFY(server.listen());
        writeInstance(dir.path(), QStringLiteral("mini-a"), server.server(), now());
        VeadotubeClient client;
        QSignalSpy states(&client, &VeadotubeClient::statesChanged);
        client.setInstancesDirForTesting(dir.path());
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.states().size(), 2, 3000);
        QVERIFY(server.messages().contains(kList));
        const QVariantMap talking = client.states().at(1).toMap();
        QCOMPARE(talking.value(QStringLiteral("id")).toString(), QStringLiteral("s2"));
        QCOMPARE(talking.value(QStringLiteral("name")).toString(), QStringLiteral("Talking"));
        QCOMPARE(states.size(), 1);
    }

    void setStateAndPushToTalk()
    {
        QTemporaryDir dir;
        FakeVeadoServer server;
        QVERIFY(server.listen());
        writeInstance(dir.path(), QStringLiteral("mini-a"), server.server(), now());
        VeadotubeClient client;
        client.setInstancesDirForTesting(dir.path());
        client.setPushToTalk(true);
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);
        const QString pttOn = QStringLiteral(R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":true}})");
        const QString pttOff = QStringLiteral(R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":false}})");
        QTRY_VERIFY(server.messages().contains(pttOff)); // brought in line on connect
        server.clear();

        client.setState(QStringLiteral("s2"));
        client.setTalking(true);
        client.setTalking(true); // no repeat
        QTRY_COMPARE(server.messages().size(), 2);
        QCOMPARE(server.messages().at(0),
                 QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"set","state":"s2"}})"));
        QCOMPARE(server.messages().at(1), pttOn);

        client.setTalking(false);
        QTRY_COMPARE(server.messages().size(), 3);
        QCOMPARE(server.messages().at(2), pttOff);

        // Push-to-talk off in the settings: talking doesn't touch it.
        client.setPushToTalk(false);
        client.setTalking(true);
        QTest::qWait(100);
        QCOMPARE(server.messages().size(), 3);

        // Switching push-to-talk off while talking releases it.
        client.setPushToTalk(true);
        QTRY_COMPARE(server.messages().size(), 4);
        QCOMPARE(server.messages().at(3), pttOn);
        client.setPushToTalk(false);
        QTRY_COMPARE(server.messages().size(), 5);
        QCOMPARE(server.messages().at(4), pttOff);

        // State ids are escaped.
        QCOMPARE(VeadotubeClient::setStateMessage(QStringLiteral("a\"b")),
                 QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"set","state":"a\"b"}})"));
    }

    void parsesStateListsLeniently()
    {
        QVariantList s = VeadotubeClient::parseStates(QStringLiteral(
            R"(nodes: {"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list","states":[{"id":7,"name":"Seven","thumbHash":"x"},{"id":"b"}],"extra":1}})"));
        QCOMPARE(s.size(), 2);
        QCOMPARE(s.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("7"));
        QCOMPARE(s.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("b"));
        s = VeadotubeClient::parseStates(QStringLiteral(
            R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list","value":["x","y"]}})"));
        QCOMPARE(s.size(), 2);
        QVERIFY(VeadotubeClient::parseStates(QStringLiteral(
                    R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"peek","state":"x"}})"))
                    .isEmpty());
        QVERIFY(VeadotubeClient::parseStates(QStringLiteral("garbage")).isEmpty());
    }

    void disablingReleasesPushToTalk()
    {
        QTemporaryDir dir;
        FakeVeadoServer server;
        QVERIFY(server.listen());
        writeInstance(dir.path(), QStringLiteral("mini-a"), server.server(), now());
        VeadotubeClient client;
        client.setInstancesDirForTesting(dir.path());
        client.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(client.status(), Status::Connected, 3000);
        client.setTalking(true);
        QTRY_VERIFY(server.messages().last().contains(QStringLiteral("\"value\":true")));
        client.setEnabled(false);
        QTRY_VERIFY(server.messages().last().contains(QStringLiteral("\"value\":false")));
        QCOMPARE(client.status(), Status::Off);
    }
};

QTEST_GUILESS_MAIN(TestVeadotube)
#include "test_veadotube.moc"
