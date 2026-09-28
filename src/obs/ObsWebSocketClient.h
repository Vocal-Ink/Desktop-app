#pragma once

#include <QAbstractSocket>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

class QTimer;
class QWebSocket;

// Minimal obs-websocket v5 client (bundled with OBS Studio 28+, default port 4455):
// Hello/Identify handshake with optional password, request/response correlation
// with per-request timeouts, and automatic reconnection with backoff.
class ObsWebSocketClient : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Identified };
    Q_ENUM(State)

    // Runs exactly once per request: with OBS's answer, on error, timeout or
    // disconnect (callbacks still pending when the client is destroyed are
    // dropped). On failure `responseData` holds OBS's requestStatus
    // ({"result", "code", "comment"}); failures on our side use code 0.
    using Callback = std::function<void(bool ok, const QJsonObject &responseData, const QString &error)>;

    explicit ObsWebSocketClient(QObject *parent = nullptr);
    ~ObsWebSocketClient() override;

    void connectTo(const QString &host, quint16 port, const QString &password);
    void disconnectFrom();
    State state() const { return m_state; }
    bool isIdentified() const { return m_state == State::Identified; }
    QString obsVersion() const { return m_obsVersion; } // OBS Studio version, e.g. "31.0.2"

    void setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
    void setReconnectDelays(int firstMs, int maxMs);
    void setRequestTimeout(int ms) { m_requestTimeoutMs = ms; }
    void setHandshakeTimeout(int ms) { m_handshakeTimeoutMs = ms; }

    void request(const QString &type, const QJsonObject &data = {}, Callback callback = {});

    // base64(sha256(base64(sha256(password + salt)) + challenge))
    static QString authString(const QString &password, const QString &salt, const QString &challenge);

signals:
    void stateChanged(ObsWebSocketClient::State state);
    void identified(const QString &obsVersion);
    void authenticationFailed();               // OBS closed with 4009; no automatic retry
    void errorOccurred(const QString &message); // a connection attempt failed (user-readable)
    void eventReceived(const QString &type, const QJsonObject &data);

private:
    struct Pending
    {
        QString type;
        Callback callback;
    };

    void openSocket();
    void discardSocket();
    void onSocketClosed(QWebSocket *socket);
    void onTextMessage(const QString &message);
    void onHello(const QJsonObject &d);
    void onIdentified();
    void onResponse(const QJsonObject &d);
    void send(int op, const QJsonObject &d);
    void failPending(const QString &error);
    void setState(State state);
    void scheduleReconnect();
    QString describeFailure(int closeCode, const QString &reason) const;

    QWebSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_handshakeTimer = nullptr;
    State m_state = State::Disconnected;
    QString m_host;
    quint16 m_port = 4455;
    QString m_password;
    QString m_obsVersion;
    bool m_enabled = false;       // connectTo() called and not disconnected since
    bool m_autoReconnect = true;
    bool m_authFailed = false;
    bool m_socketOpened = false;  // the WebSocket handshake completed for the current socket
    QAbstractSocket::SocketError m_socketError = QAbstractSocket::UnknownSocketError;
    QString m_timeoutError;
    int m_reconnectMinMs = 2000;
    int m_reconnectMaxMs = 30000;
    int m_reconnectDelayMs = 2000;
    int m_requestTimeoutMs = 5000;
    int m_handshakeTimeoutMs = 8000;
    quint64 m_nextRequestId = 0;
    quint64 m_connection = 0;     // bumped for every socket, to ignore stale answers
    QMap<quint64, Pending> m_pending;
};
