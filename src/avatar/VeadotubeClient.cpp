#include "avatar/VeadotubeClient.h"

// Placeholder implementation of the contract.

VeadotubeClient::VeadotubeClient(QObject *parent)
    : QObject(parent)
{
}

VeadotubeClient::~VeadotubeClient() = default;

void VeadotubeClient::setEnabled(bool) {}
void VeadotubeClient::setPushToTalk(bool) {}
void VeadotubeClient::setTalking(bool) {}
void VeadotubeClient::setInstancesDirForTesting(const QString &) {}

void VeadotubeClient::reconnect() {}
void VeadotubeClient::refreshStates() {}
void VeadotubeClient::setState(const QString &) {}
