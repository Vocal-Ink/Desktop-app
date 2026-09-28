#include "obs/OverlayServer.h"

#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>
#include <utility>

static void initOverlayResources()
{
    // core.qrc is compiled into a static library; referencing it keeps the linker from dropping it.
    Q_INIT_RESOURCE(core);
}

namespace {
constexpr qint64 kMaxHeaderBytes = 16 * 1024;
constexpr int kRequestTimeoutMs = 10000;
const char kRequestTimerName[] = "overlayRequestTimer";

struct HttpRequest
{
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    QByteArray header(const char *name) const { return headers.value(QByteArray(name)); }
};

bool parseRequest(const QByteArray &head, HttpRequest &request)
{
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(1).startsWith('/'))
        return false;
    request.method = requestLine.at(0);
    const QByteArray target = requestLine.at(1);
    const qsizetype query = target.indexOf('?');
    request.path = query < 0 ? target : target.left(query);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    return true;
}

bool hasToken(const QByteArray &headerValue, const char *token)
{
    const QList<QByteArray> parts = headerValue.split(',');
    for (const QByteArray &part : parts) {
        if (part.trimmed().compare(token, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return QByteArrayLiteral("OK");
    case 400: return QByteArrayLiteral("Bad Request");
    case 403: return QByteArrayLiteral("Forbidden");
    case 404: return QByteArrayLiteral("Not Found");
    case 405: return QByteArrayLiteral("Method Not Allowed");
    case 431: return QByteArrayLiteral("Request Header Fields Too Large");
    default: return QByteArrayLiteral("Error");
    }
}

// "127.0.0.1:7342" -> "127.0.0.1", "[::1]:7342" -> "::1"
QString hostName(const QByteArray &hostHeader)
{
    QString host = QString::fromLatin1(hostHeader).trimmed().toLower();
    if (host.startsWith(QLatin1Char('['))) {
        const qsizetype end = host.indexOf(QLatin1Char(']'));
        return end < 0 ? host : host.mid(1, end - 1);
    }
    const qsizetype colon = host.lastIndexOf(QLatin1Char(':'));
    return colon < 0 ? host : host.left(colon);
}

bool isLoopbackName(const QString &host)
{
    if (host == QLatin1String("localhost"))
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

QByteArray toJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray resource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

OverlayServer::OverlayServer(QObject *parent)
    : QObject(parent)
{
    static const bool resourcesReady = [] {
        initOverlayResources();
        return true;
    }();
    Q_UNUSED(resourcesReady)
}

OverlayServer::~OverlayServer()
{
    blockSignals(true);
    stop();
}

bool OverlayServer::start(quint16 port, bool allowLan)
{
    stop();
    m_error.clear();
    m_allowLan = allowLan;
    if (!m_http) {
        m_http = new QTcpServer(this);
        m_http->setProxy(QNetworkProxy::NoProxy);
        connect(m_http, &QTcpServer::newConnection, this, &OverlayServer::onNewConnection);
    }
    if (!m_ws) {
        // Never listens itself: upgrade requests arrive through the HTTP port.
        m_ws = new QWebSocketServer(QStringLiteral("Vocal Ink"), QWebSocketServer::NonSecureMode, this);
        connect(m_ws, &QWebSocketServer::newConnection, this, &OverlayServer::onWebSocketConnection);
    }
    const QHostAddress address(allowLan ? QHostAddress::Any : QHostAddress::LocalHost);
    if (m_http->listen(address, port))
        return true;
    if (m_http->serverError() == QAbstractSocket::AddressInUseError)
        m_error = tr("Port %1 is already in use by another program. Choose a different overlay port.").arg(port);
    else
        m_error = m_http->errorString();
    return false;
}

void OverlayServer::stop()
{
    if (m_http)
        m_http->close();
    const QList<QPointer<QWebSocket>> clients = std::exchange(m_clients, {});
    for (const QPointer<QWebSocket> &ws : clients) {
        if (!ws)
            continue;
        ws->disconnect(this);
        ws->close(QWebSocketProtocol::CloseCodeGoingAway);
        ws->deleteLater();
    }
    if (!clients.isEmpty())
        emit clientCountChanged(0);
}

bool OverlayServer::isRunning() const
{
    return m_http && m_http->isListening();
}

quint16 OverlayServer::port() const
{
    return isRunning() ? m_http->serverPort() : 0;
}

QUrl OverlayServer::overlayUrl(const QString &query) const
{
    if (!isRunning())
        return {};
    QUrl url(QStringLiteral("http://127.0.0.1:%1/").arg(port()));
    QString q = query.trimmed();
    if (q.startsWith(QLatin1Char('?')))
        q.remove(0, 1);
    if (!q.isEmpty())
        url.setQuery(q);
    return url;
}

void OverlayServer::showCaption(quint64 id, const QString &text, const QString &voiceName)
{
    m_lastCaptionId = id;
    m_lastCaption = toJson({{QStringLiteral("type"), QStringLiteral("caption")},
                            {QStringLiteral("id"), qint64(id)},
                            {QStringLiteral("text"), text},
                            {QStringLiteral("voice"), voiceName}});
    broadcast(m_lastCaption);
}

void OverlayServer::endCaption(quint64 id)
{
    if (id == m_lastCaptionId)
        m_lastCaption.clear();
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("end")}, {QStringLiteral("id"), qint64(id)}}));
}

void OverlayServer::clearCaptions()
{
    m_lastCaption.clear();
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("clear")}}));
}

void OverlayServer::setSpeaking(bool speaking)
{
    m_speaking = speaking;
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("speaking")}, {QStringLiteral("value"), speaking}}));
}

void OverlayServer::setListening(bool listening)
{
    m_listening = listening;
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("listening")}, {QStringLiteral("value"), listening}}));
}

void OverlayServer::onNewConnection()
{
    while (QTcpSocket *socket = m_http->nextPendingConnection()) {
        auto *timer = new QTimer(socket);
        timer->setObjectName(QLatin1String(kRequestTimerName));
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
        timer->start(kRequestTimeoutMs);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleHttp(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [socket] { socket->deleteLater(); });
    }
}

void OverlayServer::handleHttp(QTcpSocket *socket)
{
    // Peek only: a WebSocket upgrade must reach QWebSocketServer untouched.
    const QByteArray buffered = socket->peek(kMaxHeaderBytes);
    const qsizetype headerEnd = buffered.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffered.size() >= kMaxHeaderBytes)
            respond(socket, 431, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Header too large"));
        return;
    }

    HttpRequest request;
    if (!parseRequest(buffered.left(headerEnd), request)) {
        respond(socket, 400, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Bad request"));
        return;
    }
    const QByteArray host = request.header("host");
    if (!isAllowedHost(host)) {
        respond(socket, 403, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Forbidden"));
        return;
    }

    if (request.path == "/ws" && hasToken(request.header("upgrade"), "websocket")) {
        if (!isAllowedOrigin(request.header("origin"), host)) {
            respond(socket, 403, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Forbidden"));
            return;
        }
        socket->disconnect(this);
        delete socket->findChild<QTimer *>(QLatin1String(kRequestTimerName), Qt::FindDirectChildrenOnly);
        socket->setParent(nullptr); // QWebSocketServer takes ownership
        // The handshake is already buffered, so the socket won't signal readyRead
        // again by itself. Qt 6.4+ nudges it in handleConnection(); make sure
        // exactly one nudge happens whatever the Qt version does.
        bool nudged = false;
        const QMetaObject::Connection probe =
            connect(socket, &QTcpSocket::readyRead, socket, [&nudged] { nudged = true; });
        m_ws->handleConnection(socket);
        disconnect(probe);
        if (!nudged)
            emit socket->readyRead();
        return;
    }

    socket->read(headerEnd + 4);
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr); // one request per connection

    const bool head = request.method == "HEAD";
    if (request.method != "GET" && !head) {
        respond(socket, 405, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Method not allowed"));
        return;
    }
    const QByteArray &path = request.path;
    if (path == "/" || path == "/index.html") {
        respond(socket, 200, QByteArrayLiteral("text/html; charset=utf-8"), resource(QStringLiteral(":/overlay/index.html")), head);
    } else if (path == "/overlay.js") {
        respond(socket, 200, QByteArrayLiteral("text/javascript; charset=utf-8"), resource(QStringLiteral(":/overlay/overlay.js")), head);
    } else if (path == "/overlay.css") {
        respond(socket, 200, QByteArrayLiteral("text/css; charset=utf-8"), resource(QStringLiteral(":/overlay/overlay.css")), head);
    } else if (path == "/state") {
        respond(socket, 200, QByteArrayLiteral("application/json"), stateJson(), head);
    } else if (path == "/health") {
        respond(socket, 200, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("ok"), head);
    } else {
        respond(socket, 404, QByteArrayLiteral("text/plain; charset=utf-8"), QByteArrayLiteral("Not found"), head);
    }
}

void OverlayServer::respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                            bool headOnly)
{
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-cache\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Connection: close\r\n\r\n";
    if (!headOnly)
        response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

void OverlayServer::onWebSocketConnection()
{
    while (QWebSocket *ws = m_ws->nextPendingConnection()) {
        ws->setParent(this);
        m_clients.append(ws);
        connect(ws, &QWebSocket::disconnected, this, [this, ws] {
            m_clients.removeAll(ws);
            ws->deleteLater();
            emit clientCountChanged(clientCount());
        });
        // Bring the new page up to date.
        if (!m_lastCaption.isEmpty())
            ws->sendTextMessage(QString::fromUtf8(m_lastCaption));
        ws->sendTextMessage(QString::fromUtf8(
            toJson({{QStringLiteral("type"), QStringLiteral("speaking")}, {QStringLiteral("value"), m_speaking}})));
        if (m_listening) {
            ws->sendTextMessage(QString::fromUtf8(
                toJson({{QStringLiteral("type"), QStringLiteral("listening")}, {QStringLiteral("value"), true}})));
        }
        emit clientCountChanged(clientCount());
    }
}

void OverlayServer::broadcast(const QByteArray &json)
{
    m_clients.removeIf([](const QPointer<QWebSocket> &ws) { return ws.isNull(); });
    const QString message = QString::fromUtf8(json);
    for (const QPointer<QWebSocket> &ws : std::as_const(m_clients))
        ws->sendTextMessage(message);
}

// Only answer requests addressed to this machine by name, so a web page that
// re-points its own domain at 127.0.0.1 (DNS rebinding) can't read the captions.
bool OverlayServer::isAllowedHost(const QByteArray &host) const
{
    if (m_allowLan || host.isEmpty())
        return true;
    return isLoopbackName(hostName(host));
}

// Browsers send an Origin with WebSocket handshakes: accept our own page (any
// host it was loaded from) and local pages, but not arbitrary websites.
bool OverlayServer::isAllowedOrigin(const QByteArray &origin, const QByteArray &host) const
{
    if (origin.isEmpty())
        return true; // not a browser
    const QUrl url(QString::fromLatin1(origin));
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        return false;
    if (url.authority().compare(QString::fromLatin1(host), Qt::CaseInsensitive) == 0)
        return true;
    return isLoopbackName(url.host());
}

QByteArray OverlayServer::stateJson() const
{
    const QJsonDocument caption = QJsonDocument::fromJson(m_lastCaption);
    return toJson({{QStringLiteral("caption"), caption.isObject() ? QJsonValue(caption.object()) : QJsonValue()},
                   {QStringLiteral("speaking"), m_speaking},
                   {QStringLiteral("listening"), m_listening},
                   {QStringLiteral("clients"), clientCount()}});
}

// --- Profiles, styles and extra events (placeholders; implemented by the overlay work) ---

// Placeholders of the new contract; the overlay engine fills them in.

void OverlayServer::setProfiles(const QList<OverlayProfile> &resolved)
{
    m_profiles = resolved;
}

void OverlayServer::setStyle(const QString &profileId, const QJsonObject &resolvedStyle)
{
    for (OverlayProfile &p : m_profiles) {
        if (p.id == profileId)
            p.style = resolvedStyle;
    }
}

QUrl OverlayServer::profileUrl(const QString &profileId) const
{
    return overlayUrl(profileId.isEmpty() || profileId == QLatin1String("main")
                          ? QString()
                          : QStringLiteral("profile=") + profileId);
}

void OverlayServer::setLegacyQuery(const QString &query)
{
    m_legacyQuery = query;
}

void OverlayServer::setAssets(const QHash<QString, QString> &assetIdToPath)
{
    m_assets = assetIdToPath;
}

void OverlayServer::setFontDir(const QString &dir)
{
    m_fontDir = dir;
}

void OverlayServer::setAllowedHosts(const QStringList &hostNames)
{
    m_allowedHosts = hostNames;
}

void OverlayServer::setLabels(const QJsonObject &labels)
{
    m_labels = labels;
}

void OverlayServer::sendProgress(quint64, double, qint64, qint64, bool) {}
void OverlayServer::sendLevel(double, const QString &) {}
void OverlayServer::setTalking(bool) {}
void OverlayServer::setMicLive(bool) {}
void OverlayServer::chatMessage(const QJsonObject &) {}
void OverlayServer::chatDelete(const QString &) {}
void OverlayServer::chatClearUser(const QString &) {}
void OverlayServer::chatClear() {}
