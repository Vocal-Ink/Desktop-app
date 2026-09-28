#include "support/FakeObsServer.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {
QString randomBase64()
{
    QByteArray bytes(32, '\0');
    for (char &b : bytes)
        b = char(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(bytes.toBase64());
}

QWebSocketProtocol::CloseCode closeCode(int code)
{
    return static_cast<QWebSocketProtocol::CloseCode>(code);
}
} // namespace

FakeObsServer::Response FakeObsServer::Response::success(const QJsonObject &data, int delayMs)
{
    Response r;
    r.data = data;
    r.delayMs = delayMs;
    return r;
}

FakeObsServer::Response FakeObsServer::Response::failure(int code, const QString &comment)
{
    Response r;
    r.ok = false;
    r.code = code;
    r.comment = comment;
    return r;
}

FakeObsServer::Response FakeObsServer::Response::noAnswer()
{
    Response r;
    r.silent = true;
    return r;
}

FakeObsServer::FakeObsServer(QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("obs-websocket"), QWebSocketServer::NonSecureMode, this))
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    m_server->setSupportedSubprotocols({QStringLiteral("obswebsocket.json")});
#endif
    connect(m_server, &QWebSocketServer::newConnection, this, &FakeObsServer::onNewConnection);
}

FakeObsServer::~FakeObsServer() = default;

bool FakeObsServer::listen()
{
    return m_server->isListening() || m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 FakeObsServer::port() const
{
    return m_server->serverPort();
}

QList<FakeObsServer::Request> FakeObsServer::requests(const QString &type) const
{
    QList<Request> out;
    for (const Request &r : m_requests) {
        if (r.type == type)
            out.append(r);
    }
    return out;
}

QStringList FakeObsServer::requestTypes() const
{
    QStringList out;
    for (const Request &r : m_requests) {
        if (r.type != QLatin1String("GetVersion"))
            out << r.type;
    }
    return out;
}

int FakeObsServer::clientCount() const
{
    int n = 0;
    for (const QPointer<QWebSocket> &s : m_sockets) {
        if (s && s->state() == QAbstractSocket::ConnectedState)
            ++n;
    }
    return n;
}

void FakeObsServer::disconnectClients(int code, const QString &reason)
{
    for (const QPointer<QWebSocket> &s : std::as_const(m_sockets)) {
        if (s)
            s->close(closeCode(code), reason);
    }
}

QString FakeObsServer::expectedAuth(const QString &password, const QString &salt, const QString &challenge)
{
    QCryptographicHash secretHash(QCryptographicHash::Sha256);
    secretHash.addData(password.toUtf8());
    secretHash.addData(salt.toUtf8());
    const QByteArray secret = secretHash.result().toBase64();

    QCryptographicHash authHash(QCryptographicHash::Sha256);
    authHash.addData(secret);
    authHash.addData(challenge.toUtf8());
    return QString::fromLatin1(authHash.result().toBase64());
}

void FakeObsServer::onNewConnection()
{
    while (QWebSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        m_sockets.append(socket);
        m_lastSubprotocol = socket->subprotocol();
        Client client;
        QJsonObject hello{{QStringLiteral("obsWebSocketVersion"), QStringLiteral("5.5.4")},
                          {QStringLiteral("rpcVersion"), 1}};
        if (!m_password.isEmpty()) {
            client.salt = randomBase64();
            client.challenge = randomBase64();
            hello.insert(QStringLiteral("authentication"),
                         QJsonObject{{QStringLiteral("challenge"), client.challenge}, {QStringLiteral("salt"), client.salt}});
        }
        m_clients.insert(socket, client);
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString &message) { onMessage(socket, message); });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            m_sockets.removeAll(socket);
            socket->deleteLater();
        });
        ++m_helloCount;
        send(socket, 0, hello);
    }
}

void FakeObsServer::onMessage(QWebSocket *socket, const QString &message)
{
    const QJsonObject root = QJsonDocument::fromJson(message.toUtf8()).object();
    const QJsonObject d = root.value(QStringLiteral("d")).toObject();
    switch (root.value(QStringLiteral("op")).toInt(-1)) {
    case 1:
        onIdentify(socket, d);
        break;
    case 6:
        onRequest(socket, d);
        break;
    default:
        socket->close(closeCode(4006), QStringLiteral("Unknown op code"));
        break;
    }
}

void FakeObsServer::onIdentify(QWebSocket *socket, const QJsonObject &d)
{
    m_lastIdentify = d;
    Client &client = m_clients[socket];
    if (d.value(QStringLiteral("rpcVersion")).toInt() != 1) {
        socket->close(closeCode(4010), QStringLiteral("Unsupported RPC version"));
        return;
    }
    if (!m_password.isEmpty()) {
        const QString expected = expectedAuth(m_password, client.salt, client.challenge);
        if (d.value(QStringLiteral("authentication")).toString() != expected) {
            ++m_authFailures;
            socket->close(closeCode(4009), QStringLiteral("Authentication failed."));
            return;
        }
    }
    client.identified = true;
    ++m_identifiedCount;
    send(socket, 2, {{QStringLiteral("negotiatedRpcVersion"), 1}});
}

void FakeObsServer::onRequest(QWebSocket *socket, const QJsonObject &d)
{
    if (!m_clients.value(socket).identified) {
        socket->close(closeCode(4007), QStringLiteral("Not identified"));
        return;
    }
    Request request;
    request.type = d.value(QStringLiteral("requestType")).toString();
    request.id = d.value(QStringLiteral("requestId")).toString();
    request.data = d.value(QStringLiteral("requestData")).toObject();
    m_requests.append(request);
    emit requestReceived(request.type);

    const Response response = responseFor(request);
    if (response.silent)
        return;
    QJsonObject status{{QStringLiteral("result"), response.ok}, {QStringLiteral("code"), response.code}};
    if (!response.comment.isEmpty())
        status.insert(QStringLiteral("comment"), response.comment);
    QJsonObject answer{{QStringLiteral("requestType"), request.type},
                       {QStringLiteral("requestId"), request.id},
                       {QStringLiteral("requestStatus"), status}};
    if (response.ok && !response.data.isEmpty())
        answer.insert(QStringLiteral("responseData"), response.data);

    QPointer<QWebSocket> guard(socket);
    if (response.delayMs <= 0) {
        send(socket, 7, answer);
        return;
    }
    QTimer::singleShot(response.delayMs, this, [this, guard, answer] {
        if (guard)
            send(guard, 7, answer);
    });
}

FakeObsServer::Response FakeObsServer::responseFor(const Request &request)
{
    QList<Response> &queued = m_queued[request.type];
    if (!queued.isEmpty())
        return queued.takeFirst();
    if (m_responses.contains(request.type))
        return m_responses.value(request.type);
    if (request.type == QLatin1String("GetVersion")) {
        return Response::success({{QStringLiteral("obsVersion"), m_obsVersion},
                                  {QStringLiteral("obsWebSocketVersion"), QStringLiteral("5.5.4")},
                                  {QStringLiteral("rpcVersion"), 1}});
    }
    return Response::success();
}

void FakeObsServer::send(QWebSocket *socket, int op, const QJsonObject &d)
{
    const QJsonObject message{{QStringLiteral("op"), op}, {QStringLiteral("d"), d}};
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}
