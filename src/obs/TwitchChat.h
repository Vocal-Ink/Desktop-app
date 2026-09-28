#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QWebSocket;

// Reads a Twitch channel's chat anonymously (no login) so it can be read aloud
// with its own voice. Uses Twitch's IRC-over-WebSocket endpoint.
class TwitchChat : public QObject
{
    Q_OBJECT
public:
    struct Message
    {
        QString login;       // lower-case user name
        QString displayName;
        QString text;
        QString color;       // "#rrggbb" or empty
        bool broadcaster = false;
        bool moderator = false;
        bool subscriber = false;
        bool vip = false;
        bool action = false; // "/me waves"
    };

    struct Filter
    {
        bool readUserName = true;   // "alice says: ..."
        bool skipCommands = true;   // messages starting with "!"
        bool skipLinks = true;
        bool subscribersOnly = false;
        bool moderatorsOnly = false;
        int maxLength = 200;        // longer messages are cut with an ellipsis
        QStringList ignoredUsers;   // e.g. bots: nightbot, streamelements
        QStringList blockedWords;   // messages containing these (whole words) are skipped
    };

    explicit TwitchChat(QObject *parent = nullptr);
    ~TwitchChat() override;

    void connectTo(const QString &channel);
    void disconnectFrom();
    bool isConnected() const;
    QString channel() const { return m_channel; }

    void setFilter(const Filter &filter) { m_filter = filter; }
    Filter filter() const { return m_filter; }

    // Returns the text to speak, or an empty string if the filter rejects it.
    static QString speechFor(const Message &message, const Filter &filter);

    void setServerUrl(const QString &url); // tests
    void setReconnectDelays(int firstMs, int maxMs); // tests

signals:
    void statusChanged(bool connected, const QString &status);
    void messageReceived(const TwitchChat::Message &message);
    // A message that passed the filter, ready to speak.
    void speakRequested(const QString &text, const TwitchChat::Message &message);

private:
    void openSocket();
    void onSocketClosed(QWebSocket *socket);
    void handleLine(const QString &line);

    class Private;
    Private *d;
    QString m_channel;
    Filter m_filter;
};
