#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;
class QWebSocket;
class QWebSocketServer;

// Tiny local web server for the OBS "Browser Source" caption overlay.
//   GET /            overlay page (styled via query parameters, see resources/overlay)
//   GET /overlay.js, /overlay.css
//   GET /state       current caption as JSON (debugging)
//   WS  /ws          live events pushed to the page
// Binds to 127.0.0.1 unless LAN access is allowed (OBS on another PC).
class OverlayServer : public QObject
{
    Q_OBJECT
public:
    explicit OverlayServer(QObject *parent = nullptr);
    ~OverlayServer() override;

    bool start(quint16 port, bool allowLan = false);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString errorString() const { return m_error; }
    QUrl overlayUrl(const QString &query = QString()) const;
    int clientCount() const { return int(m_clients.size()); }

    // Events pushed to every connected overlay page.
    void showCaption(quint64 id, const QString &text, const QString &voiceName);
    void endCaption(quint64 id);
    void clearCaptions();
    void setSpeaking(bool speaking);
    void setListening(bool listening);

signals:
    void clientCountChanged(int count);

private:
    void onNewConnection();
    void onWebSocketConnection();
    void handleHttp(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                 bool headOnly = false);
    void broadcast(const QByteArray &json);
    bool isAllowedHost(const QByteArray &host) const;
    bool isAllowedOrigin(const QByteArray &origin, const QByteArray &host) const;
    QByteArray stateJson() const;

    QTcpServer *m_http = nullptr;
    QWebSocketServer *m_ws = nullptr;
    QList<QPointer<QWebSocket>> m_clients;
    QString m_error;
    QByteArray m_lastCaption; // replayed to pages that connect mid-sentence
    quint64 m_lastCaptionId = 0;
    bool m_allowLan = false;
    bool m_speaking = false;
    bool m_listening = false;
};
