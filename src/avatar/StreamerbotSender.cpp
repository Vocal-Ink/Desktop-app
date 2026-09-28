#include "avatar/StreamerbotSender.h"

// Placeholder implementation of the contract.

StreamerbotSender::StreamerbotSender(QObject *parent)
    : QObject(parent)
{
}

void StreamerbotSender::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void StreamerbotSender::setTarget(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void StreamerbotSender::doAction(const QString &, const QVariantMap &) {}

QByteArray StreamerbotSender::actionPayload(const QString &, const QVariantMap &, const QString &)
{
    return {};
}
