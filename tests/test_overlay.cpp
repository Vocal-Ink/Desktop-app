#include "obs/OverlayServer.h"
#include "obs/TwitchChat.h"

#include "Version.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>
#include <cmath>
#include <utility>

namespace {

QByteArray pngBytes(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

OverlayProfile profile(const QString &id, const QString &kind, const QJsonObject &style = {})
{
    return {id, id, kind, OverlayStyle::effective(style, kind)};
}

QList<OverlayProfile> threeProfiles()
{
    return {profile(QStringLiteral("main"), QStringLiteral("captions")),
            profile(QStringLiteral("chat"), QStringLiteral("chat")),
            profile(QStringLiteral("me"), QStringLiteral("avatar"))};
}

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

    bool open(quint16 port, const QString &profile = QString())
    {
        QUrl url(QStringLiteral("ws://127.0.0.1:%1/ws").arg(port));
        if (!profile.isEmpty())
            url.setQuery(QStringLiteral("profile=") + profile);
        socket.open(url);
        return QTest::qWaitFor([this] { return socket.state() == QAbstractSocket::ConnectedState; }, 3000);
    }

    // Types of the messages received so far, in order.
    QStringList types() const
    {
        QStringList out;
        for (const QJsonObject &m : messages)
            out << m.value(QStringLiteral("type")).toString();
        return out;
    }

    int count(const QString &type) const { return int(types().count(type)); }

    // Waits until nothing new arrives for `quietMs`.
    void settle(int quietMs = 150)
    {
        qsizetype seen = -1;
        while (seen != messages.size()) {
            seen = messages.size();
            QTest::qWait(quietMs);
        }
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
        // The page itself is never cached (a new app version brings a new page),
        // and it may only talk to this server.
        QCOMPARE(index.headers.value("cache-control"), QByteArray("no-store"));
        const QByteArray csp = index.headers.value("content-security-policy");
        QVERIFY(csp.contains("default-src 'self'"));
        QVERIFY(csp.contains("script-src 'self';"));
        QVERIFY(!csp.contains("unsafe-eval"));
        QVERIFY(csp.contains("connect-src 'self' ws://127.0.0.1:" + QByteArray::number(port)));
        QVERIFY(csp.contains("img-src 'self' data:"));
        QVERIFY(csp.contains("font-src 'self' data:"));
        QVERIFY(index.body.contains("<script src=\"/overlay.js\"></script>"));
        QVERIFY(!index.body.contains("<script>")); // no inline script (the CSP forbids it)
        QVERIFY(!index.body.contains("http://") && !index.body.contains("https://"));
        QCOMPARE(index.headers.value("content-length").toInt(), int(index.body.size()));

        QCOMPARE(get(port, "/index.html").body, index.body);
        // Any profile gets the same page; it asks for its profile over the WebSocket.
        QCOMPARE(get(port, "/?profile=chat").body, index.body);
        QCOMPARE(get(port, "/?profile=does-not-exist&demo=1").status, 200);

        const HttpResult js = get(port, "/overlay.js");
        QCOMPARE(js.status, 200);
        QVERIFY(js.headers.value("content-type").contains("javascript"));
        QCOMPARE(js.headers.value("cache-control"), QByteArray("no-cache"));
        QVERIFY(js.body.contains("WebSocket"));

        const HttpResult css = get(port, "/overlay.css");
        QCOMPARE(css.status, 200);
        QVERIFY(css.headers.value("content-type").startsWith("text/css"));
        QVERIFY(css.body.contains("#vi-root"));
        QVERIFY(css.body.contains(".vi-word"));

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

        QCOMPARE(get(port, "/", "localhost.:" + QByteArray::number(port)).status, 200);
        // Not even an IP address of this PC, while LAN access is off.
        QCOMPARE(get(port, "/", "192.168.1.20:" + QByteArray::number(port)).status, 403);

        // With LAN access the page may be loaded through an IP address, this PC's
        // own names, or a name the user listed. Anything else could be a web page
        // that re-pointed its own domain at this PC (DNS rebinding).
        QVERIFY(server.start(port, true));
        const QByteArray p = ":" + QByteArray::number(port);
        QCOMPARE(get(port, "/", "192.168.1.20" + p).status, 200);
        QCOMPARE(get(port, "/", "[fe80::1]" + p).status, 200);
        QCOMPARE(get(port, "/", "localhost" + p).status, 200);
        QCOMPARE(get(port, "/", "gaming-pc.local" + p).status, 403);
        QCOMPARE(get(port, "/", "evil.example" + p).status, 403);
        const QByteArray own = QHostInfo::localHostName().toLower().toUtf8();
        if (!own.isEmpty()) {
            QCOMPARE(get(port, "/", own + p).status, 200);
            QCOMPARE(get(port, "/", own.toUpper() + p).status, 200);
            QCOMPARE(get(port, "/", own + ".local" + p).status, 200);
            const QByteArray domain = QHostInfo::localDomainName().toLower().toUtf8();
            if (!domain.isEmpty())
                QCOMPARE(get(port, "/", own + '.' + domain + p).status, 200);
        }

        server.setAllowedHosts({QStringLiteral("Gaming-PC.local"), QStringLiteral(" streambox. ")});
        QCOMPARE(get(port, "/", "gaming-pc.local" + p).status, 200);
        QCOMPARE(get(port, "/", "GAMING-PC.LOCAL" + p).status, 200);
        QCOMPARE(get(port, "/", "streambox" + p).status, 200);
        QCOMPARE(get(port, "/", "gaming-pc.local.evil.example" + p).status, 403);
        QCOMPARE(get(port, "/", "unknown-name" + p).status, 403);

        // The allow list only matters in LAN mode.
        QVERIFY(server.start(port, false));
        QCOMPARE(get(port, "/", "gaming-pc.local" + p).status, 403);
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
        // Protocol v2 replays the whole state on connect, "not listening" included.
        QCOMPARE(page.take(QStringLiteral("listening")).value(QStringLiteral("value")).toBool(), false);

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
        QTRY_VERIFY(late.messages.size() >= 6);
        late.settle();
        QCOMPARE(late.types(), QStringList({QStringLiteral("hello"), QStringLiteral("config"), QStringLiteral("caption"),
                                            QStringLiteral("speaking"), QStringLiteral("listening"),
                                            QStringLiteral("mic")}));
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

    void helloAndConfigPerProfile()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        const quint16 port = server.port();
        const QList<OverlayProfile> profiles = threeProfiles();
        server.setProfiles(profiles);
        server.setLegacyQuery(QStringLiteral("style=subtitles"));
        server.setLabels(QJsonObject{{QStringLiteral("speaking"), QStringLiteral("spricht…")}});

        Page main, chat, avatar, unknown;
        QVERIFY(main.open(port));
        QVERIFY(chat.open(port, QStringLiteral("chat")));
        QVERIFY(avatar.open(port, QStringLiteral("me")));
        QVERIFY(unknown.open(port, QStringLiteral("nope")));

        const QJsonObject hello = main.take(QStringLiteral("hello"));
        QCOMPARE(hello.value(QStringLiteral("v")).toInt(), 2);
        QCOMPARE(hello.value(QStringLiteral("build")).toString(), server.build());
        QVERIFY(server.build().startsWith(QStringLiteral(VOCALINK_VERSION "+")));

        const QJsonObject config = main.take(QStringLiteral("config"));
        const QJsonObject p = config.value(QStringLiteral("profile")).toObject();
        QCOMPARE(p.value(QStringLiteral("id")).toString(), QStringLiteral("main"));
        QCOMPARE(p.value(QStringLiteral("name")).toString(), QStringLiteral("main"));
        QCOMPARE(p.value(QStringLiteral("kind")).toString(), QStringLiteral("captions"));
        QCOMPARE(config.value(QStringLiteral("style")).toObject(), profiles.at(0).style);
        const QJsonObject presets = config.value(QStringLiteral("presets")).toObject();
        QCOMPARE(presets.keys().size(), OverlayStyle::presetNames().size());
        QCOMPARE(presets.value(QStringLiteral("ink")).toObject(), OverlayStyle::preset(QStringLiteral("ink")));
        QCOMPARE(config.value(QStringLiteral("legacyQuery")).toString(), QStringLiteral("style=subtitles"));
        QVERIFY(config.value(QStringLiteral("fonts")).isArray());
        QCOMPARE(config.value(QStringLiteral("labels")).toObject().value(QStringLiteral("speaking")).toString(),
                 QStringLiteral("spricht…"));

        const QJsonObject chatConfig = chat.take(QStringLiteral("config"));
        QCOMPARE(chatConfig.value(QStringLiteral("profile")).toObject().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("chat"));
        QCOMPARE(chatConfig.value(QStringLiteral("presets")).toObject().value(QStringLiteral("outline")).toObject(),
                 OverlayStyle::presetForKind(QStringLiteral("outline"), QStringLiteral("chat")));
        QCOMPARE(avatar.take(QStringLiteral("config")).value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("me"));
        // Unknown profiles get the main one.
        QCOMPARE(unknown.take(QStringLiteral("config")).value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("main"));

        for (Page *page : {&main, &chat, &avatar, &unknown}) {
            page->settle();
            page->messages.clear();
        }

        // A style edit reaches the pages of that profile right away, and only those.
        const QJsonObject neon =
            OverlayStyle::effective(QJsonObject{{QStringLiteral("preset"), QStringLiteral("neon")}}, QStringLiteral("captions"));
        server.setStyle(QStringLiteral("main"), neon);
        QCOMPARE(main.take(QStringLiteral("config")).value(QStringLiteral("style")).toObject(), neon);
        QCOMPARE(unknown.take(QStringLiteral("config")).value(QStringLiteral("style")).toObject(), neon);
        chat.settle();
        avatar.settle();
        QCOMPARE(chat.count(QStringLiteral("config")), 0);
        QCOMPARE(avatar.count(QStringLiteral("config")), 0);

        // Nothing changed: nothing is sent.
        QList<OverlayProfile> same = profiles;
        same[0].style = neon;
        server.setStyle(QStringLiteral("main"), neon);
        server.setProfiles(same);
        server.setLegacyQuery(QStringLiteral("style=subtitles"));
        server.setLabels(QJsonObject{{QStringLiteral("speaking"), QStringLiteral("spricht…")}});
        main.settle();
        QCOMPARE(main.count(QStringLiteral("config")), 0);

        // Labels and the legacy query reach every page.
        server.setLegacyQuery(QStringLiteral("style=ink"));
        for (Page *page : {&main, &chat, &avatar, &unknown})
            QCOMPARE(page->take(QStringLiteral("config")).value(QStringLiteral("legacyQuery")).toString(), QStringLiteral("style=ink"));

        // A deleted profile's pages fall back to main.
        server.setProfiles({same.at(0), same.at(2)});
        const QJsonObject fallback = chat.take(QStringLiteral("config"));
        QCOMPARE(fallback.value(QStringLiteral("profile")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("main"));
        QCOMPARE(fallback.value(QStringLiteral("style")).toObject(), neon);
    }

    void replayDependsOnTheKind()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        server.setSpeaking(true);
        server.setMicLive(true);
        server.setTalking(true);
        server.showCaption(1, QStringLiteral("Hello chat"), QStringLiteral("Aria"));
        for (int i = 1; i <= 25; ++i) {
            server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("m%1").arg(i)},
                                           {QStringLiteral("login"), QStringLiteral("viewer")},
                                           {QStringLiteral("name"), QStringLiteral("Viewer")},
                                           {QStringLiteral("text"), QStringLiteral("message number %1").arg(i)}});
        }

        const QStringList common = {QStringLiteral("hello"), QStringLiteral("config")};
        const QStringList state = {QStringLiteral("speaking"), QStringLiteral("listening"), QStringLiteral("mic")};

        Page captions;
        QVERIFY(captions.open(server.port()));
        QTRY_VERIFY(captions.messages.size() >= 6);
        captions.settle();
        QCOMPARE(captions.types(), common + QStringList{QStringLiteral("caption")} + state);

        Page avatar;
        QVERIFY(avatar.open(server.port(), QStringLiteral("me")));
        QTRY_VERIFY(avatar.messages.size() >= 6);
        avatar.settle();
        QCOMPARE(avatar.types(), common + state + QStringList{QStringLiteral("avatar")});
        const QJsonObject a = avatar.take(QStringLiteral("avatar"));
        QCOMPARE(a.value(QStringLiteral("talking")).toBool(), true);
        QCOMPARE(a.value(QStringLiteral("mic")).toBool(), true);

        Page chat;
        QVERIFY(chat.open(server.port(), QStringLiteral("chat")));
        QTRY_VERIFY(chat.messages.size() >= 26);
        chat.settle();
        QStringList expected = common + QStringList{QStringLiteral("caption")} + state;
        for (int i = 0; i < OverlayServer::kChatReplay; ++i)
            expected << QStringLiteral("chat");
        QCOMPARE(chat.types(), expected);
        // The last 20, oldest first, with their age.
        QCOMPARE(chat.messages.at(6).value(QStringLiteral("id")).toString(), QStringLiteral("m6"));
        QCOMPARE(chat.messages.last().value(QStringLiteral("id")).toString(), QStringLiteral("m25"));
        QVERIFY(chat.messages.last().value(QStringLiteral("age")).toDouble() >= 0);
    }

    void aPageThatChangesKindGetsItsState()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QList<OverlayProfile> profiles = threeProfiles();
        profiles << profile(QStringLiteral("x"), QStringLiteral("captions"));
        server.setProfiles(profiles);
        server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")}, {QStringLiteral("text"), QStringLiteral("hi")}});
        Page page;
        QVERIFY(page.open(server.port(), QStringLiteral("x")));
        QTRY_VERIFY(page.types().contains(QStringLiteral("mic")));
        page.settle();
        QCOMPARE(page.count(QStringLiteral("chat")), 0);
        page.messages.clear();

        // The streamer turns the overlay into a chat overlay: the page rebuilds
        // itself and gets what a chat page gets on connect.
        profiles.last() = profile(QStringLiteral("x"), QStringLiteral("chat"));
        server.setProfiles(profiles);
        QTRY_VERIFY(page.types().contains(QStringLiteral("chat")));
        page.settle();
        QCOMPARE(page.types(), QStringList({QStringLiteral("config"), QStringLiteral("speaking"), QStringLiteral("listening"),
                                            QStringLiteral("mic"), QStringLiteral("chat")}));
        QCOMPARE(page.messages.first().value(QStringLiteral("profile")).toObject().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("chat"));
    }

    void chatGoesToChatPagesOnly()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        Page chat, captions, avatar;
        QVERIFY(chat.open(server.port(), QStringLiteral("chat")));
        QVERIFY(captions.open(server.port()));
        QVERIFY(avatar.open(server.port(), QStringLiteral("me")));

        server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("abc-1")},
                                       {QStringLiteral("login"), QStringLiteral("Cool_Gamer")},
                                       {QStringLiteral("name"), QStringLiteral("Cool_Gamer")},
                                       {QStringLiteral("color"), QStringLiteral("#1E90FF")},
                                       {QStringLiteral("text"), QStringLiteral("hello everyone")},
                                       {QStringLiteral("badges"), QJsonArray{QStringLiteral("sub"), QStringLiteral("BAD BADGE!"),
                                                                             QStringLiteral("mod")}},
                                       {QStringLiteral("action"), true}});
        const QJsonObject m = chat.take(QStringLiteral("chat"));
        QCOMPARE(m.value(QStringLiteral("id")).toString(), QStringLiteral("abc-1"));
        QCOMPARE(m.value(QStringLiteral("login")).toString(), QStringLiteral("cool_gamer"));
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Cool_Gamer"));
        QCOMPARE(m.value(QStringLiteral("color")).toString(), QStringLiteral("#1e90ff"));
        QCOMPARE(m.value(QStringLiteral("text")).toString(), QStringLiteral("hello everyone"));
        QCOMPARE(m.value(QStringLiteral("badges")).toArray(), (QJsonArray{QStringLiteral("sub"), QStringLiteral("mod")}));
        QCOMPARE(m.value(QStringLiteral("action")).toBool(), true);
        captions.settle();
        avatar.settle();
        QCOMPARE(captions.count(QStringLiteral("chat")), 0);
        QCOMPARE(avatar.count(QStringLiteral("chat")), 0);

        // Chat is untrusted: everything is checked and capped.
        server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("<script>")},
                                       {QStringLiteral("login"), QStringLiteral("bad login!")},
                                       {QStringLiteral("color"), QStringLiteral("red; background: url(x)")},
                                       {QStringLiteral("text"), QString(600, QLatin1Char('a')) + QChar(7)},
                                       {QStringLiteral("badges"), QStringLiteral("not an array")}});
        const QJsonObject bad = chat.take(QStringLiteral("chat"));
        QVERIFY(bad.value(QStringLiteral("id")).toString().startsWith(QLatin1String("local-")));
        QCOMPARE(bad.value(QStringLiteral("login")).toString(), QString());
        QCOMPARE(bad.value(QStringLiteral("color")).toString(), QString());
        QCOMPARE(bad.value(QStringLiteral("text")).toString(), QString(500, QLatin1Char('a')));
        QCOMPARE(bad.value(QStringLiteral("badges")).toArray().size(), 0);

        server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("abc-2")},
                                       {QStringLiteral("login"), QStringLiteral("cool_gamer")},
                                       {QStringLiteral("text"), QStringLiteral("second")}});
        server.chatMessage(QJsonObject{{QStringLiteral("id"), QStringLiteral("xyz-1")},
                                       {QStringLiteral("login"), QStringLiteral("other")},
                                       {QStringLiteral("text"), QStringLiteral("third")}});
        const auto replayIds = [&server] {
            Page fresh;
            if (!fresh.open(server.port(), QStringLiteral("chat")))
                return QStringList{QStringLiteral("failed")};
            (void)QTest::qWaitFor([&] { return fresh.types().contains(QStringLiteral("mic")); }, 3000);
            fresh.settle();
            QStringList ids;
            for (const QJsonObject &msg : std::as_const(fresh.messages)) {
                if (msg.value(QStringLiteral("type")).toString() == QLatin1String("chat"))
                    ids << msg.value(QStringLiteral("id")).toString();
            }
            return ids;
        };
        QCOMPARE(replayIds().size(), 4);

        // Moderation removes messages from the pages and from the replay.
        server.chatDelete(QStringLiteral("abc-1"));
        QCOMPARE(chat.take(QStringLiteral("chatDelete")).value(QStringLiteral("id")).toString(), QStringLiteral("abc-1"));
        QVERIFY(!replayIds().contains(QStringLiteral("abc-1")));
        server.chatClearUser(QStringLiteral("COOL_GAMER"));
        QCOMPARE(chat.take(QStringLiteral("chatClearUser")).value(QStringLiteral("login")).toString(), QStringLiteral("cool_gamer"));
        QCOMPARE(replayIds().size(), 2);
        QVERIFY(!replayIds().contains(QStringLiteral("abc-2")));
        server.chatClear();
        QVERIFY(!chat.take(QStringLiteral("chatClear")).isEmpty());
        QVERIFY(replayIds().isEmpty());
        captions.settle();
        QCOMPARE(captions.count(QStringLiteral("chatClear")), 0);
    }

    void chatMessagesAreLinkedToTheirCaption()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        Page chat;
        QVERIFY(chat.open(server.port(), QStringLiteral("chat")));
        const auto say = [&server](const char *id, const char *login, const char *text) {
            server.chatMessage(QJsonObject{{QStringLiteral("id"), QString::fromLatin1(id)},
                                           {QStringLiteral("login"), QString::fromLatin1(login)},
                                           {QStringLiteral("name"), QString::fromLatin1(login)},
                                           {QStringLiteral("text"), QString::fromUtf8(text)}});
        };
        say("m1", "anna", "first message here");
        say("m2", "ben", "is anyone playing tonight?");

        server.showCaption(1, QStringLiteral("Something I typed myself"), QStringLiteral("Aria"));
        QVERIFY(!chat.take(QStringLiteral("caption")).contains(QStringLiteral("chatId")));
        server.endCaption(1);

        // The app reads "ben says: …": that's m2 (m1 was skipped).
        server.showCaption(2, QStringLiteral("ben says: is anyone playing tonight?"), QStringLiteral("Aria"));
        QCOMPARE(chat.take(QStringLiteral("caption")).value(QStringLiteral("chatId")).toString(), QStringLiteral("m2"));
        server.endCaption(2);

        // Reading may start before the chat message reaches the server.
        server.showCaption(3, QStringLiteral("Cleo says: good game everyone, see you tomorrow"), QStringLiteral("Aria"));
        QVERIFY(!chat.take(QStringLiteral("caption")).contains(QStringLiteral("chatId")));
        chat.settle();
        chat.messages.clear();
        say("m3", "cleo", "good game everyone, see you tomorrow");
        QCOMPARE(chat.take(QStringLiteral("chat")).value(QStringLiteral("captionId")).toInt(), 3);
        Page late;
        QVERIFY(late.open(server.port(), QStringLiteral("chat")));
        QCOMPARE(late.take(QStringLiteral("caption")).value(QStringLiteral("chatId")).toString(), QStringLiteral("m3"));
    }

    void progressIsThrottled()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        Page captions, chat, avatar;
        QVERIFY(captions.open(server.port()));
        QVERIFY(chat.open(server.port(), QStringLiteral("chat")));
        QVERIFY(avatar.open(server.port(), QStringLiteral("me")));
        server.showCaption(1, QStringLiteral("Hello there everyone"), QStringLiteral("Aria"));
        captions.settle();

        // A burst: the first goes out at once, the latest follows shortly.
        for (int i = 0; i <= 50; ++i)
            server.sendProgress(1, i / 50.0, i * 20, 1000, true);
        QTest::qWait(250);
        captions.settle();
        QList<QJsonObject> progress;
        for (const QJsonObject &m : std::as_const(captions.messages)) {
            if (m.value(QStringLiteral("type")).toString() == QLatin1String("progress"))
                progress << m;
        }
        QVERIFY2(progress.size() >= 1 && progress.size() <= 3, qPrintable(QString::number(progress.size())));
        QCOMPARE(progress.last().value(QStringLiteral("f")).toDouble(), 1.0);
        QCOMPARE(progress.last().value(QStringLiteral("id")).toInt(), 1);
        QCOMPARE(progress.last().value(QStringLiteral("ms")).toInt(), 1000);
        QCOMPARE(progress.last().value(QStringLiteral("total")).toInt(), 1000);
        QCOMPARE(progress.last().value(QStringLiteral("known")).toBool(), true);
        QVERIFY(chat.count(QStringLiteral("progress")) >= 1);
        avatar.settle();
        QCOMPARE(avatar.count(QStringLiteral("progress")), 0);

        // Steady updates: at most ~15 per second.
        captions.messages.clear();
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < 60; ++i) {
            server.sendProgress(1, i / 60.0, i * 10, 600, false);
            QTest::qWait(5);
        }
        const qint64 elapsed = clock.elapsed();
        captions.settle();
        const int sent = captions.count(QStringLiteral("progress"));
        QVERIFY2(sent <= elapsed / 60 + 2, qPrintable(QStringLiteral("%1 in %2 ms").arg(sent).arg(elapsed)));
        QVERIFY(sent >= 2);

        // After the end, a late progress for that caption is dropped.
        server.sendProgress(1, 0.5, 300, 600, true);
        server.sendProgress(1, 0.6, 360, 600, true);
        server.endCaption(1);
        captions.settle();
        QCOMPARE(captions.messages.last().value(QStringLiteral("type")).toString(), QStringLiteral("end"));
    }

    void levelsAreThrottledAndEndWithZero()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QList<OverlayProfile> profiles = threeProfiles();
        profiles << profile(QStringLiteral("wave"), QStringLiteral("captions"),
                            QJsonObject{{QStringLiteral("indicator"),
                                         QJsonObject{{QStringLiteral("show"), true}, {QStringLiteral("style"), QStringLiteral("wave")}}}});
        server.setProfiles(profiles);
        Page captions, wave, avatar;
        QVERIFY(captions.open(server.port()));
        QVERIFY(wave.open(server.port(), QStringLiteral("wave")));
        QVERIFY(avatar.open(server.port(), QStringLiteral("me")));
        avatar.settle();
        avatar.messages.clear();

        for (int i = 0; i < 100; ++i)
            server.sendLevel(0.5 + 0.4 * std::sin(i * 0.7), QStringLiteral("A"));
        server.sendLevel(0.0);
        QTest::qWait(200);
        avatar.settle();
        QVERIFY2(avatar.count(QStringLiteral("level")) <= 3, qPrintable(QString::number(avatar.count(QStringLiteral("level")))));
        QCOMPARE(avatar.messages.last().value(QStringLiteral("type")).toString(), QStringLiteral("level"));
        QCOMPARE(avatar.messages.last().value(QStringLiteral("v")).toDouble(), 0.0);
        QCOMPARE(avatar.messages.last().value(QStringLiteral("viseme")).toString(), QString());
        wave.settle();
        QVERIFY(wave.count(QStringLiteral("level")) >= 1);
        captions.settle();
        QCOMPARE(captions.count(QStringLiteral("level")), 0);

        // The dead band drops changes too small to see.
        avatar.messages.clear();
        server.sendLevel(0.5, QStringLiteral("A"));
        QTest::qWait(70);
        server.sendLevel(0.51, QStringLiteral("A"));
        QTest::qWait(70);
        server.sendLevel(0.515, QStringLiteral("A"));
        QTest::qWait(70);
        avatar.settle();
        QCOMPARE(avatar.count(QStringLiteral("level")), 1);
        QCOMPARE(avatar.messages.last().value(QStringLiteral("v")).toDouble(), 0.5);
        QCOMPARE(avatar.messages.last().value(QStringLiteral("viseme")).toString(), QStringLiteral("A"));
        server.sendLevel(0.0);
        QTRY_COMPARE(avatar.messages.last().value(QStringLiteral("v")).toDouble(), 0.0);

        // Steady movement: at most 20 per second.
        avatar.messages.clear();
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < 60; ++i) {
            server.sendLevel(i % 2 ? 0.8 : 0.2, QStringLiteral("O"));
            QTest::qWait(5);
        }
        server.sendLevel(0.0);
        const qint64 elapsed = clock.elapsed();
        QTest::qWait(120);
        avatar.settle();
        const int sent = avatar.count(QStringLiteral("level"));
        QVERIFY2(sent <= elapsed / 50 + 2, qPrintable(QStringLiteral("%1 in %2 ms").arg(sent).arg(elapsed)));
        QCOMPARE(avatar.messages.last().value(QStringLiteral("v")).toDouble(), 0.0);
    }

    void avatarAndMicState()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        Page captions, avatar;
        QVERIFY(captions.open(server.port()));
        QVERIFY(avatar.open(server.port(), QStringLiteral("me")));
        captions.settle();
        avatar.settle();
        captions.messages.clear();
        avatar.messages.clear();

        server.setTalking(true);
        QJsonObject a = avatar.take(QStringLiteral("avatar"));
        QCOMPARE(a.value(QStringLiteral("talking")).toBool(), true);
        QCOMPARE(a.value(QStringLiteral("mic")).toBool(), false);
        server.setTalking(true); // no change, no message
        server.setMicLive(true);
        QCOMPARE(captions.take(QStringLiteral("mic")).value(QStringLiteral("live")).toBool(), true);
        QCOMPARE(avatar.take(QStringLiteral("mic")).value(QStringLiteral("live")).toBool(), true);
        a = avatar.take(QStringLiteral("avatar"));
        QCOMPARE(a.value(QStringLiteral("talking")).toBool(), true);
        QCOMPARE(a.value(QStringLiteral("mic")).toBool(), true);
        avatar.settle();
        captions.settle();
        QCOMPARE(avatar.count(QStringLiteral("avatar")), 0);
        QCOMPARE(captions.count(QStringLiteral("avatar")), 0);

        const QJsonObject state = QJsonDocument::fromJson(get(server.port(), "/state").body).object();
        QCOMPARE(state.value(QStringLiteral("mic")).toBool(), true);
        QCOMPARE(state.value(QStringLiteral("talking")).toBool(), true);
    }

    void servesFontsFromTheFontDir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char *name : {"Lexend-SemiBold.ttf", "BricolageGrotesque-Display.ttf", "Custom-BoldItalic.woff2",
                                 "OpenDyslexic-Regular.otf", "MyFont.woff", "notes.txt"})
            QVERIFY(writeFile(QDir(dir.path()).filePath(QString::fromLatin1(name)), QByteArray("font:") + name));

        OverlayServer server;
        QVERIFY(server.start(0));
        Page page;
        QVERIFY(page.open(server.port()));
        page.take(QStringLiteral("config"));
        server.setFontDir(dir.path());
        const QJsonArray fonts = page.take(QStringLiteral("config")).value(QStringLiteral("fonts")).toArray();
        QCOMPARE(fonts, server.fonts());
        QCOMPARE(fonts.size(), 5);

        QHash<QString, QJsonObject> byUrl;
        for (const QJsonValue &f : fonts)
            byUrl.insert(f.toObject().value(QStringLiteral("url")).toString(), f.toObject());
        const auto face = [&](const char *file) { return byUrl.value(QStringLiteral("/fonts/") + QString::fromLatin1(file)); };
        QCOMPARE(face("BricolageGrotesque-Display.ttf").value(QStringLiteral("family")).toString(), QStringLiteral("Vocal Ink Display"));
        QCOMPARE(face("BricolageGrotesque-Display.ttf").value(QStringLiteral("weight")).toInt(), 800);
        QCOMPARE(face("Lexend-SemiBold.ttf").value(QStringLiteral("family")).toString(), QStringLiteral("Lexend"));
        QCOMPARE(face("Lexend-SemiBold.ttf").value(QStringLiteral("weight")).toInt(), 600);
        QCOMPARE(face("OpenDyslexic-Regular.otf").value(QStringLiteral("family")).toString(), QStringLiteral("OpenDyslexic"));
        QCOMPARE(face("OpenDyslexic-Regular.otf").value(QStringLiteral("weight")).toInt(), 400);
        QCOMPARE(face("Custom-BoldItalic.woff2").value(QStringLiteral("weight")).toInt(), 700);
        QCOMPARE(face("Custom-BoldItalic.woff2").value(QStringLiteral("style")).toString(), QStringLiteral("italic"));
        QCOMPARE(face("MyFont.woff").value(QStringLiteral("family")).toString(), QStringLiteral("My Font"));

        const quint16 port = server.port();
        const HttpResult ttf = get(port, "/fonts/Lexend-SemiBold.ttf");
        QCOMPARE(ttf.status, 200);
        QCOMPARE(ttf.headers.value("content-type"), QByteArray("font/ttf"));
        QCOMPARE(ttf.body, QByteArray("font:Lexend-SemiBold.ttf"));
        QCOMPARE(get(port, "/fonts/OpenDyslexic-Regular.otf").headers.value("content-type"), QByteArray("font/otf"));
        QCOMPARE(get(port, "/fonts/Custom-BoldItalic.woff2").headers.value("content-type"), QByteArray("font/woff2"));
        QCOMPARE(get(port, "/fonts/MyFont.woff").headers.value("content-type"), QByteArray("font/woff"));
        QCOMPARE(get(port, "/fonts/notes.txt").status, 404);
        QCOMPARE(get(port, "/fonts/missing.ttf").status, 404);
        QCOMPARE(get(port, "/fonts/").status, 404);
        QCOMPARE(get(port, "/fonts/../fonts/Lexend-SemiBold.ttf").status, 404);
        QCOMPARE(get(port, "/fonts/%2e%2e%2fLexend-SemiBold.ttf").status, 404);
        QCOMPARE(get(port, "/fonts/sub/Lexend-SemiBold.ttf").status, 404);

        // The app's own fonts aren't in this test binary: an empty list, no errors.
        OverlayServer bare;
        QVERIFY(bare.fonts().isEmpty() || bare.fonts().first().toObject().contains(QStringLiteral("family")));
    }

    void servesAvatarAssetsFromTheListOnly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray png = pngBytes(16, 16);
        const QString pngPath = dir.filePath(QStringLiteral("0123456789abcdef.png"));
        QVERIFY(writeFile(pngPath, png));
        const QString svgPath = dir.filePath(QStringLiteral("aaaaaaaaaaaaaaaa.png"));
        QVERIFY(writeFile(svgPath, "<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>"));
        const QString bigPath = dir.filePath(QStringLiteral("bbbbbbbbbbbbbbbb.png"));
        QVERIFY(writeFile(bigPath, png + QByteArray(int(OverlayStyle::kMaxAssetBytes), '\0')));
        const QString strayPath = dir.filePath(QStringLiteral("cccccccccccccccc.png"));
        QVERIFY(writeFile(strayPath, png));
        const QByteArray webp = QByteArray::fromBase64("UklGRhwAAABXRUJQVlA4TA8AAAAvAkAAAAcQ/Y/+ByKi/wEA");
        const QString webpPath = dir.filePath(QStringLiteral("dddddddddddddddd.webp"));
        QVERIFY(writeFile(webpPath, webp));

        OverlayServer server;
        QVERIFY(server.start(0));
        server.setAssets({{QStringLiteral("0123456789abcdef.png"), pngPath},
                          {QStringLiteral("aaaaaaaaaaaaaaaa.png"), svgPath},
                          {QStringLiteral("bbbbbbbbbbbbbbbb.png"), bigPath},
                          {QStringLiteral("dddddddddddddddd.webp"), webpPath},
                          {QStringLiteral("not-an-id"), pngPath}});
        const quint16 port = server.port();

        const HttpResult ok = get(port, "/assets/0123456789abcdef.png");
        QCOMPARE(ok.status, 200);
        QCOMPARE(ok.headers.value("content-type"), QByteArray("image/png"));
        QCOMPARE(ok.headers.value("x-content-type-options"), QByteArray("nosniff"));
        QCOMPARE(ok.body, png);
        QCOMPARE(get(port, "/assets/dddddddddddddddd.webp").headers.value("content-type"), QByteArray("image/webp"));
        QCOMPARE(get(port, "/assets/aaaaaaaaaaaaaaaa.png").status, 404); // not really an image
        QCOMPARE(get(port, "/assets/bbbbbbbbbbbbbbbb.png").status, 404); // too big
        QCOMPARE(get(port, "/assets/cccccccccccccccc.png").status, 404); // not in the list
        QCOMPARE(get(port, "/assets/not-an-id").status, 404);
        QCOMPARE(get(port, "/assets/../assets/0123456789abcdef.png").status, 404);
        QCOMPARE(get(port, "/assets/0123456789abcdef.png/").status, 404);
        QCOMPARE(get(port, "/assets/0123456789ABCDEF.png").status, 404);
    }

    void limitsPagesAndIgnoresWhatTheySend()
    {
        OverlayServer server;
        QVERIFY(server.start(0));
        QList<Page *> pages;
        for (int i = 0; i < OverlayServer::kMaxPages; ++i) {
            pages << new Page;
            QVERIFY(pages.last()->open(server.port()));
        }
        QTRY_COMPARE(server.clientCount(), OverlayServer::kMaxPages);
        Page extra;
        extra.socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(server.port())));
        QTRY_COMPARE_WITH_TIMEOUT(extra.socket.state(), QAbstractSocket::UnconnectedState, 5000);
        QCOMPARE(server.clientCount(), OverlayServer::kMaxPages);
        QCOMPARE(extra.count(QStringLiteral("hello")), 0);

        // Pages can't change anything by talking.
        Page *talker = pages.first();
        talker->socket.sendTextMessage(QStringLiteral("{\"type\":\"caption\",\"id\":1,\"text\":\"injected\"}"));
        QTest::qWait(100);
        QVERIFY(QJsonDocument::fromJson(get(server.port(), "/state").body).object().value(QStringLiteral("caption")).isNull());
        QCOMPARE(talker->socket.state(), QAbstractSocket::ConnectedState);
        // ...and big messages get them disconnected.
        talker->socket.sendTextMessage(QString(4096, QLatin1Char('x')));
        QTRY_COMPARE(server.clientCount(), OverlayServer::kMaxPages - 1);

        // A free slot can be used again.
        QVERIFY(extra.open(server.port()));
        QTRY_COMPARE(server.clientCount(), OverlayServer::kMaxPages);
        qDeleteAll(pages);
    }

    void twitchModerationReachesChatPages()
    {
        QWebSocketServer twitch(QStringLiteral("fake-twitch"), QWebSocketServer::NonSecureMode);
        QVERIFY(twitch.listen(QHostAddress::LocalHost, 0));
        QWebSocket *irc = nullptr;
        connect(&twitch, &QWebSocketServer::newConnection, this, [&] { irc = twitch.nextPendingConnection(); });

        OverlayServer server;
        QVERIFY(server.start(0));
        server.setProfiles(threeProfiles());
        Page page;
        QVERIFY(page.open(server.port(), QStringLiteral("chat")));

        TwitchChat chat;
        chat.setServerUrl(QStringLiteral("ws://127.0.0.1:%1").arg(twitch.serverPort()));
        connect(&chat, &TwitchChat::speakRequested, this, [&server](const QString &, const TwitchChat::Message &m) {
            server.chatMessage(QJsonObject{{QStringLiteral("id"), m.id},
                                           {QStringLiteral("login"), m.login},
                                           {QStringLiteral("name"), m.displayName},
                                           {QStringLiteral("color"), m.color},
                                           {QStringLiteral("text"), m.text}});
        });
        connect(&chat, &TwitchChat::messageDeleted, &server, &OverlayServer::chatDelete);
        connect(&chat, &TwitchChat::userCleared, &server, &OverlayServer::chatClearUser);
        connect(&chat, &TwitchChat::chatCleared, &server, &OverlayServer::chatClear);
        QSignalSpy deleted(&chat, &TwitchChat::messageDeleted);
        QSignalSpy userCleared(&chat, &TwitchChat::userCleared);
        QSignalSpy cleared(&chat, &TwitchChat::chatCleared);
        chat.connectTo(QStringLiteral("somechannel"));
        QTRY_VERIFY(irc != nullptr);

        const QString id = QStringLiteral("885196de-cb67-427a-baa8-82f9b0fcd05f");
        irc->sendTextMessage(QStringLiteral("@badges=;color=#FF0000;display-name=Alice;id=%1 "
                                            ":alice!alice@alice.tmi.twitch.tv PRIVMSG #somechannel :hello there\r\n")
                                 .arg(id));
        const QJsonObject m = page.take(QStringLiteral("chat"));
        QCOMPARE(m.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(m.value(QStringLiteral("login")).toString(), QStringLiteral("alice"));
        QCOMPARE(m.value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));

        irc->sendTextMessage(QStringLiteral("@login=alice;room-id=;target-msg-id=%1;tmi-sent-ts=1642720582342 "
                                            ":tmi.twitch.tv CLEARMSG #somechannel :hello there\r\n")
                                 .arg(id));
        QTRY_COMPARE(deleted.size(), 1);
        QCOMPARE(deleted.first().at(0).toString(), id);
        QCOMPARE(page.take(QStringLiteral("chatDelete")).value(QStringLiteral("id")).toString(), id);

        irc->sendTextMessage(QStringLiteral("@ban-duration=600;room-id=12345678;target-user-id=87654321 "
                                            ":tmi.twitch.tv CLEARCHAT #somechannel :Alice\r\n"));
        QTRY_COMPARE(userCleared.size(), 1);
        QCOMPARE(userCleared.first().at(0).toString(), QStringLiteral("alice"));
        QCOMPARE(page.take(QStringLiteral("chatClearUser")).value(QStringLiteral("login")).toString(), QStringLiteral("alice"));

        irc->sendTextMessage(QStringLiteral("@room-id=12345678;tmi-sent-ts=1642715695392 :tmi.twitch.tv CLEARCHAT #somechannel\r\n"));
        QTRY_COMPARE(cleared.size(), 1);
        QVERIFY(!page.take(QStringLiteral("chatClear")).isEmpty());
        QCOMPARE(userCleared.size(), 1);
        chat.disconnectFrom();
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
