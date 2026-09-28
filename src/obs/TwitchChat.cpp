#include "obs/TwitchChat.h"

// Placeholder implementation (replaced by the text work package).
class TwitchChat::Private {};

TwitchChat::TwitchChat(QObject *parent) : QObject(parent), d(new Private) {}
TwitchChat::~TwitchChat() { delete d; }
void TwitchChat::connectTo(const QString &channel)
{
    m_channel = channel;
    emit statusChanged(false, tr("Twitch chat is not available in this build."));
}
void TwitchChat::disconnectFrom() {}
bool TwitchChat::isConnected() const { return false; }
QString TwitchChat::speechFor(const Message &, const Filter &) { return {}; }
void TwitchChat::setServerUrl(const QString &) {}
