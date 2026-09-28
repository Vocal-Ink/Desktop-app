#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

class QTcpServer;
class QTcpSocket;

// Minimal HTTP/1.1 server for tests. Each request is answered by the handler;
// the connection is closed after the response. Records every request.
class MockHttpServer : public QObject
{
    Q_OBJECT
public:
    struct Request
    {
        QByteArray method;
        QString path;              // without query string
        QUrlQuery query;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
        QByteArray header(const char *name) const { return headers.value(QByteArray(name).toLower()); }
    };
    struct Response
    {
        int status = 200;
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray body;
        // When > 1 the body is written in this many pieces with short pauses,
        // to exercise streaming consumers.
        int chunks = 1;

        static Response json(const QByteArray &body, int status = 200);
        static Response bytes(const QByteArray &body, const QByteArray &contentType, int status = 200);
    };
    using Handler = std::function<Response(const Request &)>;

    explicit MockHttpServer(QObject *parent = nullptr);
    ~MockHttpServer() override;

    bool listen();
    quint16 port() const;
    QString baseUrl() const; // "http://127.0.0.1:<port>"
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    const QList<Request> &requests() const { return m_requests; }
    void clearRequests() { m_requests.clear(); }

private:
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket, const Response &response);

    QTcpServer *m_server;
    Handler m_handler;
    QList<Request> m_requests;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
