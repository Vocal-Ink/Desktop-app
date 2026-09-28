#include "support/MockHttpServer.h"

#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

MockHttpServer::Response MockHttpServer::Response::json(const QByteArray &body, int status)
{
    Response r;
    r.status = status;
    r.body = body;
    r.headers.append({QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")});
    return r;
}

MockHttpServer::Response MockHttpServer::Response::bytes(const QByteArray &body, const QByteArray &contentType, int status)
{
    Response r;
    r.status = status;
    r.body = body;
    r.headers.append({QByteArrayLiteral("Content-Type"), contentType});
    return r;
}

MockHttpServer::MockHttpServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                m_buffers.remove(s);
                s->deleteLater();
            });
        }
    });
}

MockHttpServer::~MockHttpServer() = default;

bool MockHttpServer::listen()
{
    return m_server->listen(QHostAddress::LocalHost, 0);
}

quint16 MockHttpServer::port() const
{
    return m_server->serverPort();
}

QString MockHttpServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void MockHttpServer::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();
    const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    Request req;
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    req.method = requestLine.value(0);
    const QUrl url(QString::fromLatin1(requestLine.value(1)));
    req.path = url.path();
    req.query = QUrlQuery(url.query());
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    const qsizetype contentLength = req.headers.value("content-length", "0").toLongLong();
    const qsizetype bodyStart = headerEnd + 4;
    if (buf.size() - bodyStart < contentLength)
        return; // wait for the rest of the body
    req.body = buf.mid(bodyStart, contentLength);
    buf.clear();

    m_requests.append(req);
    const Response resp = m_handler ? m_handler(req) : Response::json("{}", 404);
    respond(socket, resp);
}

void MockHttpServer::respond(QTcpSocket *socket, const Response &response)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
    for (const auto &h : response.headers)
        head += h.first + ": " + h.second + "\r\n";
    head += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head);

    const int chunks = qMax(1, response.chunks);
    if (chunks == 1 || response.body.size() < chunks) {
        socket->write(response.body);
        socket->flush();
        socket->disconnectFromHost();
        return;
    }
    const qsizetype piece = response.body.size() / chunks;
    QPointer<QTcpSocket> guard(socket);
    for (int i = 0; i < chunks; ++i) {
        const qsizetype start = i * piece;
        const QByteArray part = (i == chunks - 1) ? response.body.mid(start) : response.body.mid(start, piece);
        QTimer::singleShot(15 * i, this, [guard, part, last = (i == chunks - 1)] {
            if (!guard)
                return;
            guard->write(part);
            guard->flush();
            if (last)
                guard->disconnectFromHost();
        });
    }
}
