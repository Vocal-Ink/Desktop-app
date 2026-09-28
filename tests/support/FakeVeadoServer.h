#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class QWebSocket;
class QWebSocketServer;

// Fake veadotube mini WebSocket server for tests: records the URL each client
// connected with and every text frame, and answers state list requests.
class FakeVeadoServer : public QObject
{
    Q_OBJECT
public:
    explicit FakeVeadoServer(QObject *parent = nullptr);
    ~FakeVeadoServer() override;

    bool listen();
    quint16 port() const;
    QString server() const; // "127.0.0.1:<port>", as in an instance file
    void disconnectClients();

    // Sent in answer to a "list" request (after "nodes:").
    QString listAnswer = QStringLiteral(
        R"({"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list","states":[{"id":"s1","name":"Idle"},{"id":"s2","name":"Talking"}]}})");

    QList<QUrl> connectionUrls() const { return m_urls; }
    QStringList messages() const { return m_messages; }
    void clear() { m_messages.clear(); }

signals:
    void messageReceived(const QString &message);

private:
    QWebSocketServer *m_server;
    QList<QPointer<QWebSocket>> m_sockets;
    QList<QUrl> m_urls;
    QStringList m_messages;
};
