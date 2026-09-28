#include "avatar/VmcSender.h"

// Placeholder implementation of the contract.

QList<VmcSender::Preset> VmcSender::presets()
{
    return {{QStringLiteral("vseeface"), QStringLiteral("VSeeFace"), 39539},
            {QStringLiteral("warudo"), QStringLiteral("Warudo"), 39539},
            {QStringLiteral("vnyan"), QStringLiteral("VNyan"), 39539},
            {QStringLiteral("vmc"), QStringLiteral("VirtualMotionCapture"), 39540},
            {QStringLiteral("custom"), QString(), 39539}}; // "custom" is named in QML
}

VmcSender::VmcSender(QObject *parent)
    : QObject(parent)
{
}

void VmcSender::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void VmcSender::setTarget(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void VmcSender::setBlendset(const QString &blendset)
{
    m_blendset = blendset;
}

void VmcSender::setGain(float gain)
{
    m_gain = gain;
}

void VmcSender::setExpression(const QString &name)
{
    m_expression = name;
}

QVariantList VmcSender::presetList() const
{
    QVariantList out;
    for (const Preset &p : presets())
        out.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("port"), p.port}});
    return out;
}

void VmcSender::sendMouth(float, AvatarController::Viseme, bool) {}
void VmcSender::release() {}

QByteArray VmcSender::oscMessage(const QByteArray &, const QVariantList &)
{
    return {};
}

QByteArray VmcSender::oscBundle(const QList<QByteArray> &)
{
    return {};
}
