#include "support/FakeVtsServer.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QWebSocket>
#include <QWebSocketServer>

FakeVtsServer::FakeVtsServer(QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("VTubeStudio"), QWebSocketServer::NonSecureMode, this))
{
    m_clock.start();
    connect(m_server, &QWebSocketServer::newConnection, this, &FakeVtsServer::onNewConnection);
}

FakeVtsServer::~FakeVtsServer() = default;

bool FakeVtsServer::listen(quint16 port)
{
    return m_server->isListening() || m_server->listen(QHostAddress::LocalHost, port);
}

quint16 FakeVtsServer::port() const
{
    return m_server->serverPort();
}

QUrl FakeVtsServer::url() const
{
    QUrl u;
    u.setScheme(QStringLiteral("ws"));
    u.setHost(QStringLiteral("127.0.0.1"));
    u.setPort(port());
    return u;
}

void FakeVtsServer::close()
{
    disconnectClients();
    m_server->close();
}

QList<FakeVtsServer::Message> FakeVtsServer::messages(const QString &type) const
{
    QList<Message> out;
    for (const Message &m : m_messages) {
        if (m.type == type)
            out.append(m);
    }
    return out;
}

QVariantMap FakeVtsServer::injected(const Message &message)
{
    QVariantMap out;
    const QJsonArray values = message.data.value(QStringLiteral("parameterValues")).toArray();
    for (const QJsonValue &v : values) {
        const QJsonObject o = v.toObject();
        out.insert(o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("value")).toDouble());
    }
    return out;
}

int FakeVtsServer::clientCount() const
{
    int n = 0;
    for (const auto &s : m_sockets) {
        if (s && s->state() == QAbstractSocket::ConnectedState)
            ++n;
    }
    return n;
}

void FakeVtsServer::disconnectClients()
{
    for (const auto &s : std::as_const(m_sockets)) {
        if (s)
            s->close(QWebSocketProtocol::CloseCodeGoingAway);
    }
    m_sockets.clear();
    m_pendingTokens.clear();
}

void FakeVtsServer::onNewConnection()
{
    while (QWebSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        m_sockets.append(socket);
        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString &text) { onMessage(socket, text); });
        connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void FakeVtsServer::reply(QWebSocket *socket, const QString &requestId, const QString &type, const QJsonObject &data)
{
    if (!socket)
        return;
    const QJsonObject message{{QStringLiteral("apiName"), QStringLiteral("VTubeStudioPublicAPI")},
                              {QStringLiteral("apiVersion"), QStringLiteral("1.0")},
                              {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()},
                              {QStringLiteral("requestID"), requestId},
                              {QStringLiteral("messageType"), type},
                              {QStringLiteral("data"), data}};
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void FakeVtsServer::error(QWebSocket *socket, const QString &requestId, int errorId, const QString &message)
{
    reply(socket, requestId, QStringLiteral("APIError"),
          {{QStringLiteral("errorID"), errorId}, {QStringLiteral("message"), message}});
}

void FakeVtsServer::allowPending()
{
    const auto pending = std::exchange(m_pendingTokens, {});
    for (const auto &p : pending) {
        validTokens.append(grantToken);
        reply(p.first, p.second, QStringLiteral("AuthenticationTokenResponse"),
              {{QStringLiteral("authenticationToken"), grantToken}});
    }
}

void FakeVtsServer::denyPending()
{
    const auto pending = std::exchange(m_pendingTokens, {});
    for (const auto &p : pending)
        error(p.first, p.second, 50, QStringLiteral("User has denied API access for your plugin."));
}

void FakeVtsServer::sendEvent(const QString &eventType, const QJsonObject &data)
{
    for (const auto &s : std::as_const(m_sockets)) {
        if (s)
            reply(s, QStringLiteral("event"), eventType, data);
    }
}

void FakeVtsServer::onMessage(QWebSocket *socket, const QString &text)
{
    const QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    Message m;
    m.type = obj.value(QStringLiteral("messageType")).toString();
    m.id = obj.value(QStringLiteral("requestID")).toString();
    m.data = obj.value(QStringLiteral("data")).toObject();
    m.atMs = m_clock.elapsed();
    m_messages.append(m);
    emit messageReceived(m.type);

    if (obj.value(QStringLiteral("apiName")).toString() != QLatin1String("VTubeStudioPublicAPI")) {
        error(socket, m.id, 0, QStringLiteral("bad apiName"));
        return;
    }
    const QString type = m.type;
    const QJsonObject &d = m.data;
    if (type == QLatin1String("AuthenticationTokenRequest")) {
        if (denyTokenRequest) {
            error(socket, m.id, 50, QStringLiteral("User has denied API access for your plugin."));
        } else if (holdTokenRequest) {
            m_pendingTokens.append({socket, m.id});
        } else {
            validTokens.append(grantToken);
            reply(socket, m.id, QStringLiteral("AuthenticationTokenResponse"),
                  {{QStringLiteral("authenticationToken"), grantToken}});
        }
    } else if (type == QLatin1String("AuthenticationRequest")) {
        const bool ok = validTokens.contains(d.value(QStringLiteral("authenticationToken")).toString());
        reply(socket, m.id, QStringLiteral("AuthenticationResponse"),
              {{QStringLiteral("authenticated"), ok},
               {QStringLiteral("reason"), ok ? QStringLiteral("Token valid.") : QStringLiteral("Token invalid.")}});
    } else if (type == QLatin1String("InjectParameterDataRequest")) {
        if (checkInjectedParameters) {
            const QJsonArray values = d.value(QStringLiteral("parameterValues")).toArray();
            for (const QJsonValue &v : values) {
                const QString id = v.toObject().value(QStringLiteral("id")).toString();
                if (!defaultParameters.contains(id) && !customParameters.contains(id)) {
                    error(socket, m.id, 453, QStringLiteral("Parameter %1 not found.").arg(id));
                    return;
                }
            }
        }
        reply(socket, m.id, QStringLiteral("InjectParameterDataResponse"), {});
    } else if (type == QLatin1String("CurrentModelRequest")) {
        reply(socket, m.id, QStringLiteral("CurrentModelResponse"),
              {{QStringLiteral("modelLoaded"), !modelName.isEmpty()}, {QStringLiteral("modelName"), modelName},
               {QStringLiteral("modelID"), QStringLiteral("model-1")}});
    } else if (type == QLatin1String("HotkeysInCurrentModelRequest")) {
        reply(socket, m.id, QStringLiteral("HotkeysInCurrentModelResponse"),
              {{QStringLiteral("modelLoaded"), true}, {QStringLiteral("modelName"), modelName},
               {QStringLiteral("availableHotkeys"), hotkeys}});
    } else if (type == QLatin1String("HotkeyTriggerRequest")) {
        const QString id = d.value(QStringLiteral("hotkeyID")).toString();
        reply(socket, m.id, QStringLiteral("HotkeyTriggerResponse"), {{QStringLiteral("hotkeyID"), id}});
    } else if (type == QLatin1String("ExpressionStateRequest")) {
        reply(socket, m.id, QStringLiteral("ExpressionStateResponse"),
              {{QStringLiteral("modelLoaded"), true}, {QStringLiteral("expressions"), expressions}});
    } else if (type == QLatin1String("ExpressionActivationRequest")) {
        reply(socket, m.id, QStringLiteral("ExpressionActivationResponse"), {});
    } else if (type == QLatin1String("InputParameterListRequest")) {
        QJsonArray defaults;
        for (const QString &p : std::as_const(defaultParameters))
            defaults.append(QJsonObject{{QStringLiteral("name"), p}, {QStringLiteral("value"), 0}});
        QJsonArray custom;
        for (const QString &p : std::as_const(customParameters))
            custom.append(QJsonObject{{QStringLiteral("name"), p}, {QStringLiteral("value"), 0}});
        reply(socket, m.id, QStringLiteral("InputParameterListResponse"),
              {{QStringLiteral("modelLoaded"), true}, {QStringLiteral("defaultParameters"), defaults},
               {QStringLiteral("customParameters"), custom}});
    } else if (type == QLatin1String("ParameterCreationRequest")) {
        const QString name = d.value(QStringLiteral("parameterName")).toString();
        if (!customParameters.contains(name))
            customParameters.append(name);
        reply(socket, m.id, QStringLiteral("ParameterCreationResponse"), {{QStringLiteral("parameterName"), name}});
    } else if (type == QLatin1String("EventSubscriptionRequest")) {
        reply(socket, m.id, QStringLiteral("EventSubscriptionResponse"),
              {{QStringLiteral("subscribedEventCount"), 1},
               {QStringLiteral("subscribedEvents"), QJsonArray{d.value(QStringLiteral("eventName"))}}});
    } else {
        error(socket, m.id, 2, QStringLiteral("Unknown request %1").arg(type));
    }
}
