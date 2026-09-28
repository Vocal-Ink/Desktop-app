#include "platform/VirtualDriver.h"

// Placeholder until the per-OS installers land.

VirtualDriver::VirtualDriver(QObject *parent)
    : QObject(parent)
{
    refresh();
}

VirtualDriver::~VirtualDriver() = default;

bool VirtualDriver::needsAdmin() const
{
    return false;
}

QString VirtualDriver::displayName() const
{
    return QStringLiteral("Vocal Ink Virtual Mic");
}

QString VirtualDriver::outputDeviceName() const
{
    return QStringLiteral("Vocal Ink Voice");
}

QString VirtualDriver::inputDeviceName() const
{
    return QStringLiteral("Vocal Ink Mic");
}

QByteArray VirtualDriver::outputDeviceId() const
{
    return {};
}

void VirtualDriver::refresh()
{
    setState(State::Unsupported, tr("No bundled virtual mic for this system yet."));
}

void VirtualDriver::install()
{
    emit finished(false, m_statusText);
}

void VirtualDriver::uninstall()
{
    emit finished(false, m_statusText);
}

void VirtualDriver::setState(State state, const QString &text)
{
    if (m_state == state && m_statusText == text)
        return;
    m_state = state;
    m_statusText = text;
    emit stateChanged();
}
