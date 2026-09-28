#include "support/FakeVeadoServer.h"

#include <QHostAddress>
#include <QWebSocket>
#include <QWebSocketServer>

FakeVeadoServer::FakeVeadoServer(QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("veadotube"), QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, [this] {
        while (QWebSocket *socket = m_server->nextPendingConnection()) {
            socket->setParent(this);
            m_sockets.append(socket);
            m_urls.append(socket->requestUrl());
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
                m_messages.append(text);
                emit messageReceived(text);
                if (text.startsWith(QLatin1String("nodes:")) && text.contains(QLatin1String("\"event\":\"list\"")))
                    socket->sendTextMessage(QStringLiteral("nodes:") + listAnswer);
            });
            connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

FakeVeadoServer::~FakeVeadoServer() = default;

bool FakeVeadoServer::listen()
{
    return m_server->isListening() || m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 FakeVeadoServer::port() const
{
    return m_server->serverPort();
}

QString FakeVeadoServer::server() const
{
    return QStringLiteral("127.0.0.1:%1").arg(port());
}

void FakeVeadoServer::disconnectClients()
{
    for (const auto &s : std::as_const(m_sockets)) {
        if (s)
            s->close();
    }
    m_sockets.clear();
}
