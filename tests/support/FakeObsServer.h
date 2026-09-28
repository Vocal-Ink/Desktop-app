#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QWebSocket;
class QWebSocketServer;

// Fake obs-websocket v5 server for tests: sends Hello (with an auth challenge
// when a password is set), validates Identify and answers requests with
// scripted responses. Records everything it receives.
class FakeObsServer : public QObject
{
    Q_OBJECT
public:
    struct Request
    {
        QString type;
        QString id;
        QJsonObject data;
    };
    struct Response
    {
        bool ok = true;
        int code = 100;
        QString comment;
        QJsonObject data;
        int delayMs = 0;
        bool silent = false; // never answer

        static Response success(const QJsonObject &data = {}, int delayMs = 0);
        static Response failure(int code, const QString &comment = QString());
        static Response noAnswer();
    };

    explicit FakeObsServer(QObject *parent = nullptr);
    ~FakeObsServer() override;

    bool listen();
    quint16 port() const;

    void setPassword(const QString &password) { m_password = password; }
    void setObsVersion(const QString &version) { m_obsVersion = version; }
    // Answer for every request of this type (default: success without data).
    void setResponse(const QString &type, const Response &response) { m_responses.insert(type, response); }
    // One-off answers, used before the setResponse() one.
    void queueResponse(const QString &type, const Response &response) { m_queued[type].append(response); }

    QList<Request> requests() const { return m_requests; }
    QList<Request> requests(const QString &type) const;
    QStringList requestTypes() const; // in arrival order, without the client's GetVersion
    void clearRequests() { m_requests.clear(); }

    int helloCount() const { return m_helloCount; }
    int identifiedCount() const { return m_identifiedCount; }
    int authFailures() const { return m_authFailures; }
    QJsonObject lastIdentify() const { return m_lastIdentify; }
    QString lastSubprotocol() const { return m_lastSubprotocol; }
    int clientCount() const;
    void disconnectClients(int closeCode = 1001, const QString &reason = QString());

    // Written independently of ObsWebSocketClient::authString().
    static QString expectedAuth(const QString &password, const QString &salt, const QString &challenge);

signals:
    void requestReceived(const QString &type);

private:
    struct Client
    {
        QString salt;
        QString challenge;
        bool identified = false;
    };

    void onNewConnection();
    void onMessage(QWebSocket *socket, const QString &message);
    void onIdentify(QWebSocket *socket, const QJsonObject &d);
    void onRequest(QWebSocket *socket, const QJsonObject &d);
    void send(QWebSocket *socket, int op, const QJsonObject &d);
    Response responseFor(const Request &request);

    QWebSocketServer *m_server;
    QList<QPointer<QWebSocket>> m_sockets;
    QHash<QWebSocket *, Client> m_clients;
    QString m_password;
    QString m_obsVersion = QStringLiteral("31.0.2");
    QHash<QString, Response> m_responses;
    QHash<QString, QList<Response>> m_queued;
    QList<Request> m_requests;
    int m_helloCount = 0;
    int m_identifiedCount = 0;
    int m_authFailures = 0;
    QJsonObject m_lastIdentify;
    QString m_lastSubprotocol;
};
