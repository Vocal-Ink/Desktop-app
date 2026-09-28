#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QPair>
#include <QUrl>
#include <QVariantMap>

class QWebSocket;
class QWebSocketServer;

// Fake VTube Studio plugin API server for tests: hands out tokens, checks
// them, answers model queries and records every request (with the time it
// arrived, for keep-alive checks).
class FakeVtsServer : public QObject
{
    Q_OBJECT
public:
    struct Message
    {
        QString type;
        QString id;
        QJsonObject data;
        qint64 atMs = 0; // since the server was created
    };

    explicit FakeVtsServer(QObject *parent = nullptr);
    ~FakeVtsServer() override;

    bool listen(quint16 port = 0);
    quint16 port() const;
    QUrl url() const;
    void close();

    // Behaviour
    QString grantToken = QStringLiteral("token-1"); // answer to AuthenticationTokenRequest
    bool denyTokenRequest = false;                  // errorID 50 instead
    bool holdTokenRequest = false;                  // answer only on allowPending()/denyPending()
    QStringList validTokens;                        // AuthenticationRequest succeeds for these (+ granted ones)
    QString modelName = QStringLiteral("Test Model");
    QJsonArray hotkeys;
    QJsonArray expressions;
    QStringList defaultParameters = {QStringLiteral("MouthOpen"), QStringLiteral("MouthSmile"),
                                     QStringLiteral("VoiceVolume"), QStringLiteral("VoiceA"),
                                     QStringLiteral("VoiceI"), QStringLiteral("VoiceU"),
                                     QStringLiteral("VoiceE"), QStringLiteral("VoiceO")};
    QStringList customParameters;
    bool checkInjectedParameters = true; // errorID 453 for unknown ids

    void allowPending();
    void denyPending();
    void sendEvent(const QString &eventType, const QJsonObject &data);
    void disconnectClients();
    int clientCount() const;

    QList<Message> messages() const { return m_messages; }
    QList<Message> messages(const QString &type) const;
    int count(const QString &type) const { return int(messages(type).size()); }
    void clear() { m_messages.clear(); }

    // parameterValues of an InjectParameterDataRequest as {id: value}.
    static QVariantMap injected(const Message &message);

signals:
    void messageReceived(const QString &type);

private:
    void onNewConnection();
    void onMessage(QWebSocket *socket, const QString &text);
    void reply(QWebSocket *socket, const QString &requestId, const QString &type, const QJsonObject &data);
    void error(QWebSocket *socket, const QString &requestId, int errorId, const QString &message);

    QWebSocketServer *m_server;
    QList<QPointer<QWebSocket>> m_sockets;
    QList<QPair<QPointer<QWebSocket>, QString>> m_pendingTokens; // socket, request id
    QList<Message> m_messages;
    QElapsedTimer m_clock;
};
