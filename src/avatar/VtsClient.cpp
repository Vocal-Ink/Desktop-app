#include "avatar/VtsClient.h"

// Placeholder implementation of the contract.

VtsClient::VtsClient(SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
{
}

VtsClient::~VtsClient() = default;

void VtsClient::setEnabled(bool) {}
void VtsClient::setPort(quint16) {}
void VtsClient::setUrlForTesting(const QUrl &) {}
void VtsClient::setMouthParameters(const QString &, const QString &) {}
void VtsClient::setFaceFound(bool) {}
void VtsClient::setCustomParameters(bool) {}

void VtsClient::reconnect() {}
void VtsClient::requestAccess() {}
void VtsClient::forgetAccess() {}
void VtsClient::refreshModelData() {}
void VtsClient::triggerHotkey(const QString &) {}
void VtsClient::setExpression(const QString &, bool) {}
void VtsClient::setMouth(float, float, const std::array<float, 5> &) {}
void VtsClient::release() {}
