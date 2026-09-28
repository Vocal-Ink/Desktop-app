#include "obs/OverlayServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QWebSocket>

namespace {

struct HttpResult
{
    int status = 0;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;
};

// Sends raw bytes (optionally in pieces) and collects the response until the server closes.
HttpResult rawRequest(quint16 port, const QList<QByteArray> &pieces)
{
    QTcpSocket socket;
    QByteArray data;
    bool closed = false;
    QObject::connect(&socket, &QTcpSocket::readyRead, [&] { data += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, [&] { closed = true; });
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!QTest::qWaitFor([&] { return socket.state() == QAbstractSocket::ConnectedState; }, 3000))
        return {};
    for (const QByteArray &piece : pieces) {
        socket.write(piece);
        socket.flush();
        QTest::qWait(30);
    }
    (void)QTest::qWaitFor([&] { return closed; }, 3000);
    data += socket.readAll();

    HttpResult result;
    const qsizetype headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return result;
    const QList<QByteArray> lines = data.left(headerEnd).split('\n');
    result.status = lines.value(0).split(' ').value(1).toInt();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            result.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
    }
    result.body = data.mid(headerEnd + 4);
    return result;
}

HttpResult get(quint16 port, const QByteArray &path, const QByteArray &host = QByteArray(), const QByteArray &method = "GET")
{
    const QByteArray hostHeader = host.isEmpty() ? "127.0.0.1:" + QByteArray::number(port) : host;
    return rawRequest(port, {method + ' ' + path + " HTTP/1.1\r\nHost: " + hostHeader + "\r\nConnection: close\r\n\r\n"});
}

// A page connected to /ws that records the JSON messages it receives.
class Page : public QObject
{
public:
    explicit Page(const QString &origin = QString())
        : socket(origin)
    {
        connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
            messages.append(QJsonDocument::fromJson(message.toUtf8()).object());
        });
    }

    bool open(quint16 port)
    {
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(port)));
        return QTest::qWaitFor([this] { return socket.state() == QAbstractSocket::ConnectedState; }, 3000);
    }

    // Waits for (and consumes) the next message of the given type.
    QJsonObject take(const QString &type, int timeoutMs = 3000)
    {
        QJsonObject found;
        (void)QTest::qWaitFor([&] {
            for (qsizetype i = 0; i < messages.size(); ++i) {
                if (messages.at(i).value(QStringLiteral("type")).toString() == type) {
                    found = messages.takeAt(i);
                    return true;
                }
            }
            return false;
        }, timeoutMs);
        return found;
    }

    QWebSocket socket;
    QList<QJsonObject> messages;
};

} // namespace

class TestOverlay : public QObject
{
    Q_OBJECT
private slots:
    void startsOnAFreePort()
    {
        OverlayServer server;
        QVERIFY(!server.isRunning());
        QVERIFY(server.overlayUrl().isEmpty());
        QVERIFY(server.start(0));
        QVERIFY(server.isRunning());
        const quint16 port = server.port();
        QVERIFY(port > 0);
        QCOMPARE(server.overlayUrl().toString(), QStringLiteral("http://127.0.0.1:%1/").arg(port));
        QCOMPARE(server.overlayUrl(QStringLiteral("style=bubble&size=50")).toString(),
                 QStringLiteral("http://127.0.0.1:%1/?style=bubble&size=50").arg(port));
        QCOMPARE(server.overlayUrl(QStringLiteral("?name=1")).toString(),
                 QStringLiteral("http://127.0.0.1:%1/?name=1").arg(port));
        server.stop();
        QVERIFY(!server.isRunning());
        QCOMPARE(server.port(), quint16(0));
    }

    void servesThePageWithARealHttpClient()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QNetworkAccessManager nam;
        QNetworkReply *reply = nam.get(QNetworkRequest(server.overlayUrl(QStringLiteral("style=bubble"))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(3000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QVERIFY(reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("text/html")));
        QVERIFY(reply->readAll().contains("<script src=\"/overlay.js\"></script>"));
        reply->deleteLater();
    }

    void servesAssets()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        const quint16 port = server.port();

        const HttpResult index = get(port, "/");
        QCOMPARE(index.status, 200);
        QVERIFY(index.headers.value("content-type").startsWith("text/html"));
        QCOMPARE(index.headers.value("cache-control"), QByteArray("no-cache"));
        QVERIFY(index.body.contains("<script src=\"/overlay.js\"></script>"));
        QCOMPARE(index.headers.value("content-length").toInt(), int(index.body.size()));

        QCOMPARE(get(port, "/index.html").body, index.body);

        const HttpResult js = get(port, "/overlay.js");
        QCOMPARE(js.status, 200);
        QVERIFY(js.headers.value("content-type").contains("javascript"));
        QCOMPARE(js.headers.value("cache-control"), QByteArray("no-cache"));
        QVERIFY(js.body.contains("WebSocket"));

        const HttpResult css = get(port, "/overlay.css");
        QCOMPARE(css.status, 200);
        QVERIFY(css.headers.value("content-type").startsWith("text/css"));
        QVERIFY(css.body.contains(".box"));

        const HttpResult health = get(port, "/health");
        QCOMPARE(health.status, 200);
        QCOMPARE(health.body, QByteArray("ok"));

        const HttpResult head = get(port, "/overlay.js", QByteArray(), "HEAD");
        QCOMPARE(head.status, 200);
        QVERIFY(head.body.isEmpty());

        QCOMPARE(get(port, "/nope").status, 404);
        QCOMPARE(get(port, "/overlay.js/../secret").status, 404);
        QCOMPARE(get(port, "/", QByteArray(), "POST").status, 405);
        QCOMPARE(rawRequest(port, {"garbage\r\n\r\n"}).status, 400);
    }

    void stateReportsTheCurrentCaption()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QJsonObject state = QJsonDocument::fromJson(get(server.port(), "/state").body).object();
        QVERIFY(state.value(QStringLiteral("caption")).isNull());
        QCOMPARE(state.value(QStringLiteral("speaking")).toBool(), false);

        server.setSpeaking(true);
        server.showCaption(5, QStringLiteral("Hello there"), QStringLiteral("Brian"));
        const HttpResult result = get(server.port(), "/state");
        QCOMPARE(result.headers.value("content-type"), QByteArray("application/json"));
        state = QJsonDocument::fromJson(result.body).object();
        QCOMPARE(state.value(QStringLiteral("caption")).toObject().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Hello there"));
        QCOMPARE(state.value(QStringLiteral("speaking")).toBool(), true);
    }

    void rejectsForeignHostNames()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        const quint16 port = server.port();
        QCOMPARE(get(port, "/", "evil.example:" + QByteArray::number(port)).status, 403);
        QCOMPARE(get(port, "/", "localhost:" + QByteArray::number(port)).status, 200);
        QCOMPARE(get(port, "/", "[::1]:" + QByteArray::number(port)).status, 200);

        // With LAN access the page may be loaded through any address of this PC.
        QVERIFY(server.start(port, true));
        QCOMPARE(get(port, "/", "gaming-pc.local:" + QByteArray::number(port)).status, 200);
    }

    void pushesEventsToPages()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QSignalSpy clients(&server, &OverlayServer::clientCountChanged);

        Page page;
        QVERIFY(page.open(server.port()));
        QTRY_COMPARE(server.clientCount(), 1);
        QCOMPARE(clients.last().at(0).toInt(), 1);
        QCOMPARE(page.take(QStringLiteral("speaking")).value(QStringLiteral("value")).toBool(), false);

        server.setSpeaking(true);
        QCOMPARE(page.take(QStringLiteral("speaking")).value(QStringLiteral("value")).toBool(), true);

        server.showCaption(7, QStringLiteral("Hello there, chat"), QStringLiteral("Brian"));
        const QJsonObject caption = page.take(QStringLiteral("caption"));
        QCOMPARE(caption.value(QStringLiteral("id")).toInt(), 7);
        QCOMPARE(caption.value(QStringLiteral("text")).toString(), QStringLiteral("Hello there, chat"));
        QCOMPARE(caption.value(QStringLiteral("voice")).toString(), QStringLiteral("Brian"));

        server.endCaption(7);
        QCOMPARE(page.take(QStringLiteral("end")).value(QStringLiteral("id")).toInt(), 7);

        server.setListening(true);
        QCOMPARE(page.take(QStringLiteral("listening")).value(QStringLiteral("value")).toBool(), true);

        server.clearCaptions();
        QVERIFY(!page.take(QStringLiteral("clear")).isEmpty());

        page.socket.close();
        QTRY_COMPARE(server.clientCount(), 0);
        QCOMPARE(clients.last().at(0).toInt(), 0);
    }

    void lateJoinersSeeTheCurrentCaption()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setSpeaking(true);
        server.showCaption(3, QStringLiteral("Mid sentence"), QStringLiteral("Aria"));

        Page late;
        QVERIFY(late.open(server.port()));
        const QJsonObject caption = late.take(QStringLiteral("caption"));
        QCOMPARE(caption.value(QStringLiteral("id")).toInt(), 3);
        QCOMPARE(caption.value(QStringLiteral("text")).toString(), QStringLiteral("Mid sentence"));
        QCOMPARE(late.take(QStringLiteral("speaking")).value(QStringLiteral("value")).toBool(), true);

        // Once the sentence is over, new pages start empty.
        server.endCaption(3);
        server.setSpeaking(false);
        Page later;
        QVERIFY(later.open(server.port()));
        QCOMPARE(later.take(QStringLiteral("speaking")).value(QStringLiteral("value")).toBool(), false);
        QTest::qWait(50);
        QVERIFY(later.take(QStringLiteral("caption"), 0).isEmpty());
    }

    void upgradeSplitAcrossPackets()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());
        const QByteArray request = "GET /ws HTTP/1.1\r\nHost: " + host
            + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

        QTcpSocket socket;
        QByteArray data;
        connect(&socket, &QTcpSocket::readyRead, this, [&] { data += socket.readAll(); });
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        socket.write(request.left(40));
        socket.flush();
        QTest::qWait(60);
        socket.write(request.mid(40));
        socket.flush();
        QTRY_VERIFY_WITH_TIMEOUT(data.contains("\r\n\r\n"), 3000);
        QVERIFY(data.startsWith("HTTP/1.1 101"));
        QVERIFY(data.contains("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
        QTRY_COMPARE(server.clientCount(), 1);
    }

    void rejectsPagesFromOtherWebsites()
    {
        OverlayServer server;
        QVERIFY(server.start(0));

        Page evil(QStringLiteral("https://evil.example"));
        QVERIFY(!evil.open(server.port()));
        QCOMPARE(server.clientCount(), 0);

        Page own(QStringLiteral("http://127.0.0.1:%1").arg(server.port()));
        QVERIFY(own.open(server.port()));
        QTRY_COMPARE(server.clientCount(), 1);
    }

    void reportsAPortInUse()
    {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        OverlayServer server;
        QVERIFY(!server.start(blocker.serverPort()));
        QVERIFY(!server.isRunning());
        QVERIFY(server.errorString().contains(QString::number(blocker.serverPort())));
    }

    void stopDisconnectsPages()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        Page page;
        QVERIFY(page.open(server.port()));
        QTRY_COMPARE(server.clientCount(), 1);
        QSignalSpy clients(&server, &OverlayServer::clientCountChanged);
        server.stop();
        QCOMPARE(server.clientCount(), 0);
        QCOMPARE(clients.size(), 1);
        QTRY_COMPARE(page.socket.state(), QAbstractSocket::UnconnectedState);
    }
};

QTEST_GUILESS_MAIN(TestOverlay)
#include "test_overlay.moc"
