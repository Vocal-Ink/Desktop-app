#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QUdpSocket;

// Runs Streamer.bot actions when Vocal Ink starts or stops speaking or the
// real mic goes live, so anything Streamer.bot controls (VTube Studio, T.I.T.S.,
// lights, scenes...) can react. Uses Streamer.bot's UDP server (Servers/Clients
// -> UDP Server, default 127.0.0.1:4242), which needs no password:
//   {"request":"DoAction","action":{"name":"<action>"},"args":{...},"id":"<n>"}
class StreamerbotSender : public QObject
{
    Q_OBJECT
public:
    explicit StreamerbotSender(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setTarget(const QString &host, quint16 port);
    bool isEnabled() const { return m_enabled; }

    // Does nothing when disabled or the name is empty.
    void doAction(const QString &actionName, const QVariantMap &args = {});

    static QByteArray actionPayload(const QString &actionName, const QVariantMap &args, const QString &id);

private:
    QUdpSocket *m_socket = nullptr;
    bool m_enabled = false;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 4242;
    quint64 m_nextId = 1;
};
