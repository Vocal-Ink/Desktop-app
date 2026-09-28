#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "avatar/AvatarController.h"

class QUdpSocket;

// Sends mouth blendshapes with the VMC protocol (OSC over UDP) to VSeeFace,
// Warudo, VNyan, VirtualMotionCapture and anything else with a VMC receiver.
// Each frame: /VMC/Ext/Blend/Val ,sf <name> <value> for every mouth shape (and
// the optional expression), then /VMC/Ext/Blend/Apply — in one OSC bundle.
class VmcSender : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY configChanged)
    Q_PROPERTY(QString host READ host NOTIFY configChanged)
    Q_PROPERTY(int port READ port NOTIFY configChanged)
    Q_PROPERTY(bool sending READ isSending NOTIFY statusChanged) // frames went out in the last second
public:
    struct Preset
    {
        QString id;   // "vseeface" | "warudo" | "vnyan" | "vmc" | "custom"
        QString name; // "VSeeFace"
        int port;     // the receiver's default port
    };
    static QList<Preset> presets();
    Q_INVOKABLE QVariantList presetList() const; // [{id, name, port}]

    explicit VmcSender(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setTarget(const QString &host, quint16 port);
    void setBlendset(const QString &blendset); // "vrm0" | "vrm1"
    void setGain(float gain);                  // 0.2..2
    void setExpression(const QString &name);   // held at 1 while talking ("" = none)

    bool isEnabled() const { return m_enabled; }
    QString host() const { return m_host; }
    int port() const { return m_port; }
    bool isSending() const { return m_sending; }

    // One frame; open 0..1 spread over the viseme (and its neighbours).
    void sendMouth(float open, AvatarController::Viseme viseme, bool talking);
    void release(); // all mouth shapes and the expression back to 0

    // OSC encoding (public for tests). Args: QString -> s, float/double -> f,
    // int -> i, bool -> T/F. Strings are NUL-terminated and padded to 4 bytes,
    // numbers big-endian.
    static QByteArray oscMessage(const QByteArray &address, const QVariantList &args);
    static QByteArray oscBundle(const QList<QByteArray> &messages); // "#bundle", immediate time tag

signals:
    void configChanged();
    void statusChanged();

private:
    QUdpSocket *m_socket = nullptr;
    bool m_enabled = false;
    QString m_host = QStringLiteral("127.0.0.1");
    int m_port = 39539;
    QString m_blendset = QStringLiteral("vrm0");
    float m_gain = 1.0f;
    QString m_expression;
    bool m_sending = false;
};
