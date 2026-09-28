#include "obs/ObsWebSocketClient.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <utility>
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
#include <QWebSocketHandshakeOptions>
#endif

namespace {
constexpr int kOpHello = 0;
constexpr int kOpIdentify = 1;
constexpr int kOpIdentified = 2;
constexpr int kOpEvent = 5;
constexpr int kOpRequest = 6;
constexpr int kOpRequestResponse = 7;
constexpr int kRpcVersion = 1;
constexpr int kCloseAuthenticationFailed = 4009;
constexpr int kCloseUnsupportedRpcVersion = 4010;

QJsonObject localFailure()
{
    return QJsonObject{{QStringLiteral("result"), false}, {QStringLiteral("code"), 0}};
}

bool isLocalHost(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}
} // namespace

ObsWebSocketClient::ObsWebSocketClient(QObject *parent)
    : QObject(parent)
    , m_reconnectTimer(new QTimer(this))
    , m_handshakeTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ObsWebSocketClient::openSocket);

    m_handshakeTimer->setSingleShot(true);
    connect(m_handshakeTimer, &QTimer::timeout, this, [this] {
        QWebSocket *socket = m_socket;
        if (!socket)
            return;
        if (m_socketOpened)
            m_timeoutError = tr("OBS did not answer. Vocal Ink needs OBS Studio 28 or newer (WebSocket server version 5).");
        const quint64 connection = m_connection;
        socket->abort();
        if (connection == m_connection && m_socket == socket)
            onSocketClosed(socket);
    });
}

ObsWebSocketClient::~ObsWebSocketClient()
{
    m_pending.clear();
    discardSocket();
}

void ObsWebSocketClient::setReconnectDelays(int firstMs, int maxMs)
{
    m_reconnectMinMs = qMax(10, firstMs);
    m_reconnectMaxMs = qMax(m_reconnectMinMs, maxMs);
    m_reconnectDelayMs = m_reconnectMinMs;
}

void ObsWebSocketClient::connectTo(const QString &host, quint16 port, const QString &password)
{
    const QString trimmed = host.trimmed();
    m_host = trimmed.isEmpty() ? QStringLiteral("127.0.0.1") : trimmed;
    m_port = port ? port : 4455;
    m_password = password;
    m_enabled = true;
    m_authFailed = false;
    m_reconnectDelayMs = m_reconnectMinMs;
    m_reconnectTimer->stop();
    discardSocket();
    failPending(tr("Reconnecting to OBS"));
    openSocket();
}

void ObsWebSocketClient::disconnectFrom()
{
    m_enabled = false;
    m_reconnectTimer->stop();
    m_handshakeTimer->stop();
    discardSocket();
    setState(State::Disconnected);
    failPending(tr("Disconnected from OBS"));
}

void ObsWebSocketClient::openSocket()
{
    discardSocket();
    ++m_connection;
    m_socketOpened = false;
    m_socketError = QAbstractSocket::UnknownSocketError;
    m_timeoutError.clear();
    m_obsVersion.clear();

    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket->setProxy(QNetworkProxy::NoProxy); // OBS is local or on the LAN
    m_socket = socket;
    const quint64 connection = m_connection;
    connect(socket, &QWebSocket::connected, this, [this] { m_socketOpened = true; });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] { onSocketClosed(socket); });
    connect(socket, &QWebSocket::textMessageReceived, this, &ObsWebSocketClient::onTextMessage);
    const auto onError = [this, socket, connection](QAbstractSocket::SocketError error) {
        m_socketError = error;
        // A refused connection does not always end with disconnected().
        QTimer::singleShot(0, this, [this, socket, connection] {
            if (connection == m_connection && m_socket == socket && socket->state() == QAbstractSocket::UnconnectedState)
                onSocketClosed(socket);
        });
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QWebSocket::errorOccurred, this, onError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, onError);
#endif

    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(m_host);
    url.setPort(m_port);
    setState(State::Connecting);
    m_handshakeTimer->start(m_handshakeTimeoutMs);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QStringLiteral("obswebsocket.json")});
    socket->open(url, options);
#else
    socket->open(url);
#endif
}

void ObsWebSocketClient::discardSocket()
{
    if (!m_socket)
        return;
    QWebSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        // Let queued requests (e.g. hiding the indicator) and the close frame go
        // out; the socket outlives this client for a moment if necessary.
        socket->setParent(nullptr);
        connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(2000, socket, &QObject::deleteLater);
        socket->flush();
        socket->close();
    } else {
        socket->abort();
        socket->deleteLater();
    }
}

void ObsWebSocketClient::onSocketClosed(QWebSocket *socket)
{
    if (socket != m_socket)
        return;
    const int code = int(socket->closeCode());
    const QString reason = socket->closeReason();
    const bool wasIdentified = m_state == State::Identified;
    const bool opened = m_socketOpened;
    m_handshakeTimer->stop();
    discardSocket();
    setState(State::Disconnected);
    failPending(tr("The connection to OBS was lost"));

    if (opened && code == kCloseAuthenticationFailed) {
        m_authFailed = true;
        emit authenticationFailed();
        return;
    }
    if (!wasIdentified)
        emit errorOccurred(describeFailure(code, reason));
    scheduleReconnect();
}

QString ObsWebSocketClient::describeFailure(int closeCode, const QString &reason) const
{
    if (!m_timeoutError.isEmpty())
        return m_timeoutError;
    if (!m_socketOpened) {
        if (m_socketError == QAbstractSocket::HostNotFoundError)
            return tr("Could not find the computer “%1”. Check the OBS address in the settings.").arg(m_host);
        if (isLocalHost(m_host))
            return tr("OBS is not running or the WebSocket server is off (Tools → WebSocket Server Settings)");
        return tr("Could not reach OBS at %1:%2. Check that OBS is running, its WebSocket server is on "
                  "and the firewall allows the port.")
            .arg(m_host)
            .arg(m_port);
    }
    if (closeCode == kCloseUnsupportedRpcVersion)
        return tr("This OBS version is not supported. Please update OBS Studio.");
    if (!reason.isEmpty())
        return tr("OBS closed the connection: %1").arg(reason);
    return tr("OBS closed the connection");
}

void ObsWebSocketClient::scheduleReconnect()
{
    if (!m_enabled || !m_autoReconnect || m_authFailed)
        return;
    m_reconnectTimer->start(m_reconnectDelayMs);
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, m_reconnectMaxMs);
}

void ObsWebSocketClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void ObsWebSocketClient::onTextMessage(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QJsonObject d = root.value(QStringLiteral("d")).toObject();
    switch (root.value(QStringLiteral("op")).toInt(-1)) {
    case kOpHello:
        onHello(d);
        break;
    case kOpIdentified:
        onIdentified();
        break;
    case kOpEvent:
        emit eventReceived(d.value(QStringLiteral("eventType")).toString(), d.value(QStringLiteral("eventData")).toObject());
        break;
    case kOpRequestResponse:
        onResponse(d);
        break;
    default:
        break;
    }
}

void ObsWebSocketClient::onHello(const QJsonObject &d)
{
    QJsonObject identify{{QStringLiteral("rpcVersion"), kRpcVersion}, {QStringLiteral("eventSubscriptions"), 0}};
    const QJsonObject auth = d.value(QStringLiteral("authentication")).toObject();
    if (!auth.isEmpty()) {
        identify.insert(QStringLiteral("authentication"),
                        authString(m_password, auth.value(QStringLiteral("salt")).toString(),
                                   auth.value(QStringLiteral("challenge")).toString()));
    }
    send(kOpIdentify, identify);
}

void ObsWebSocketClient::onIdentified()
{
    m_handshakeTimer->stop();
    m_reconnectDelayMs = m_reconnectMinMs;
    setState(State::Identified);
    const quint64 connection = m_connection;
    request(QStringLiteral("GetVersion"), {}, [this, connection](bool ok, const QJsonObject &data, const QString &) {
        if (connection != m_connection || m_state != State::Identified)
            return;
        m_obsVersion = ok ? data.value(QStringLiteral("obsVersion")).toString() : QString();
        emit identified(m_obsVersion);
    });
}

void ObsWebSocketClient::onResponse(const QJsonObject &d)
{
    bool validId = false;
    const quint64 id = d.value(QStringLiteral("requestId")).toString().toULongLong(&validId);
    auto it = m_pending.find(id);
    if (!validId || it == m_pending.end())
        return;
    const Pending pending = it.value();
    m_pending.erase(it);
    if (!pending.callback)
        return;

    const QJsonObject status = d.value(QStringLiteral("requestStatus")).toObject();
    if (status.value(QStringLiteral("result")).toBool()) {
        pending.callback(true, d.value(QStringLiteral("responseData")).toObject(), QString());
        return;
    }
    const QString comment = status.value(QStringLiteral("comment")).toString();
    const int code = status.value(QStringLiteral("code")).toInt();
    pending.callback(false, status,
                     comment.isEmpty() ? tr("OBS could not do %1 (error %2)").arg(pending.type).arg(code) : comment);
}

void ObsWebSocketClient::request(const QString &type, const QJsonObject &data, Callback callback)
{
    if (m_state != State::Identified || !m_socket) {
        if (callback) {
            const QString error = tr("Not connected to OBS");
            QTimer::singleShot(0, this, [callback, error] { callback(false, localFailure(), error); });
        }
        return;
    }

    const quint64 id = ++m_nextRequestId;
    QJsonObject d{{QStringLiteral("requestType"), type}, {QStringLiteral("requestId"), QString::number(id)}};
    if (!data.isEmpty())
        d.insert(QStringLiteral("requestData"), data);
    m_pending.insert(id, Pending{type, std::move(callback)});
    QTimer::singleShot(m_requestTimeoutMs, this, [this, id] {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        const Pending pending = it.value();
        m_pending.erase(it);
        if (pending.callback)
            pending.callback(false, localFailure(), tr("OBS did not answer the %1 request in time").arg(pending.type));
    });
    send(kOpRequest, d);
}

void ObsWebSocketClient::send(int op, const QJsonObject &d)
{
    if (!m_socket)
        return;
    const QJsonObject message{{QStringLiteral("op"), op}, {QStringLiteral("d"), d}};
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void ObsWebSocketClient::failPending(const QString &error)
{
    // Callbacks may issue new requests; only fail the ones pending right now.
    const QMap<quint64, Pending> pending = std::exchange(m_pending, {});
    for (const Pending &p : pending) {
        if (p.callback)
            p.callback(false, localFailure(), error);
    }
}

QString ObsWebSocketClient::authString(const QString &password, const QString &salt, const QString &challenge)
{
    const QByteArray secret = QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256).toBase64();
    const QByteArray auth = QCryptographicHash::hash(secret + challenge.toUtf8(), QCryptographicHash::Sha256).toBase64();
    return QString::fromLatin1(auth);
}
