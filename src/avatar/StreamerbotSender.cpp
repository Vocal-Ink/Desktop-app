#include "avatar/StreamerbotSender.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUdpSocket>

StreamerbotSender::StreamerbotSender(QObject *parent)
    : QObject(parent)
{
}

void StreamerbotSender::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (!enabled) {
        delete m_socket; // opened on the first action
        m_socket = nullptr;
    }
}

void StreamerbotSender::setTarget(const QString &host, quint16 port)
{
    m_host = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    m_port = port ? port : 4242;
}

void StreamerbotSender::doAction(const QString &actionName, const QVariantMap &args)
{
    const QString name = actionName.trimmed();
    if (!m_enabled || name.isEmpty())
        return;
    const QByteArray payload = actionPayload(name, args, QString::number(m_nextId++));

    QHostAddress address(m_host);
    if (address.isNull() && m_host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        address = QHostAddress(QHostAddress::LocalHost);
    if (!m_socket)
        m_socket = new QUdpSocket(this);
    if (!address.isNull()) {
        m_socket->writeDatagram(payload, address, m_port);
        return;
    }
    // A host name (Streamer.bot on another PC): resolve, then send.
    const quint16 port = m_port;
    QHostInfo::lookupHost(m_host, this, [this, payload, port](const QHostInfo &info) {
        if (!m_enabled || !m_socket)
            return;
        const QList<QHostAddress> addresses = info.addresses();
        if (!addresses.isEmpty())
            m_socket->writeDatagram(payload, addresses.first(), port);
    });
}

QByteArray StreamerbotSender::actionPayload(const QString &actionName, const QVariantMap &args, const QString &id)
{
    const QJsonObject request{
        {QStringLiteral("request"), QStringLiteral("DoAction")},
        {QStringLiteral("action"), QJsonObject{{QStringLiteral("name"), actionName}}},
        {QStringLiteral("args"), QJsonObject::fromVariantMap(args)},
        {QStringLiteral("id"), id},
    };
    return QJsonDocument(request).toJson(QJsonDocument::Compact);
}
