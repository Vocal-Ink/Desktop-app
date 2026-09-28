#include "obs/TwitchChat.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>

Q_DECLARE_METATYPE(TwitchChat::Message)

namespace {

// Just enough of Twitch's IRC-over-WebSocket endpoint: records what the client
// sends and lets the test push raw IRC frames.
class FakeTwitch : public QObject
{
public:
    FakeTwitch()
        : m_server(QStringLiteral("fake-twitch"), QWebSocketServer::NonSecureMode)
    {
        connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
            while (QWebSocket *socket = m_server.nextPendingConnection()) {
                ++connections;
                clients << socket;
                connect(socket, &QWebSocket::textMessageReceived, this,
                        [this](const QString &message) { received << message.trimmed(); });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString url() const { return QStringLiteral("ws://127.0.0.1:%1").arg(m_server.serverPort()); }
    void send(const QString &frame) { clients.last()->sendTextMessage(frame); }
    int count(const QString &line) const { return int(received.count(line)); }
    QString nick() const
    {
        for (auto it = received.crbegin(); it != received.crend(); ++it) {
            if (it->startsWith(QLatin1String("NICK ")))
                return it->mid(5);
        }
        return {};
    }
    void close() { m_server.close(); }

    QStringList received;
    QList<QWebSocket *> clients;
    int connections = 0;

private:
    QWebSocketServer m_server;
};

TwitchChat::Message chatter(const QString &login, const QString &text)
{
    TwitchChat::Message m;
    m.login = login;
    m.displayName = login;
    m.text = text;
    return m;
}

} // namespace

class TestTwitchChat : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<TwitchChat::Message>(); }

    void readsChat()
    {
        FakeTwitch twitch;
        QVERIFY(twitch.listen());
        TwitchChat chat;
        chat.setServerUrl(twitch.url());
        chat.setReconnectDelays(50, 200);
        TwitchChat::Filter filter;
        filter.ignoredUsers = {QStringLiteral("Nightbot")};
        filter.blockedWords = {QStringLiteral("spoiler")};
        chat.setFilter(filter);
        QSignalSpy status(&chat, &TwitchChat::statusChanged);
        QSignalSpy messages(&chat, &TwitchChat::messageReceived);
        QSignalSpy speak(&chat, &TwitchChat::speakRequested);

        chat.connectTo(QStringLiteral("#SomeChannel"));
        QCOMPARE(chat.channel(), QStringLiteral("somechannel"));

        // Anonymous login handshake.
        QTRY_COMPARE(twitch.received.size(), 4);
        QCOMPARE(twitch.received.at(0), QStringLiteral("CAP REQ :twitch.tv/tags twitch.tv/commands"));
        QCOMPARE(twitch.received.at(1), QStringLiteral("PASS SCHMOOPIIE"));
        QVERIFY2(QRegularExpression(QStringLiteral("^NICK justinfan\\d{5}$")).match(twitch.received.at(2)).hasMatch(),
                 qPrintable(twitch.received.at(2)));
        QCOMPARE(twitch.received.at(3), QStringLiteral("JOIN #somechannel"));
        QVERIFY(!chat.isConnected());

        const QString nick = twitch.nick();
        twitch.send(QStringLiteral(":tmi.twitch.tv 001 %1 :Welcome, GLHF!\r\n"
                                   ":tmi.twitch.tv CAP * ACK :twitch.tv/tags twitch.tv/commands\r\n"
                                   ":%1!%1@%1.tmi.twitch.tv JOIN #somechannel\r\n")
                        .arg(nick));
        QTRY_VERIFY(chat.isConnected());
        QCOMPARE(status.last().at(0).toBool(), true);
        QCOMPARE(status.last().at(1).toString(), QStringLiteral("Reading chat from #somechannel"));

        twitch.send(QStringLiteral("PING :tmi.twitch.tv\r\n"));
        QTRY_COMPARE(twitch.count(QStringLiteral("PONG :tmi.twitch.tv")), 1);

        // Two messages in one frame: a subscriber and a moderator's command.
        twitch.send(QStringLiteral(
            "@badge-info=subscriber/8;badges=subscriber/6,premium/1;color=#1E90FF;display-name=Cool_Gamer;emotes=;"
            "mod=0;subscriber=1;user-type= :cool_gamer!cool_gamer@cool_gamer.tmi.twitch.tv PRIVMSG #somechannel "
            ":hello everyone\r\n"
            "@badges=moderator/1;color=;display-name=Mod\\sGuy;mod=1;subscriber=0 "
            ":modguy!modguy@modguy.tmi.twitch.tv PRIVMSG #somechannel :!uptime\r\n"));
        QTRY_COMPARE(messages.size(), 2);
        const auto first = messages.at(0).at(0).value<TwitchChat::Message>();
        QCOMPARE(first.login, QStringLiteral("cool_gamer"));
        QCOMPARE(first.displayName, QStringLiteral("Cool_Gamer"));
        QCOMPARE(first.text, QStringLiteral("hello everyone"));
        QCOMPARE(first.color, QStringLiteral("#1E90FF"));
        QVERIFY(first.subscriber);
        QVERIFY(!first.moderator && !first.broadcaster && !first.vip);
        const auto second = messages.at(1).at(0).value<TwitchChat::Message>();
        QCOMPARE(second.displayName, QStringLiteral("Mod Guy"));
        QVERIFY(second.moderator);
        QVERIFY(!second.subscriber);
        QCOMPARE(speak.size(), 1); // the command is skipped
        QCOMPARE(speak.first().at(0).toString(), QStringLiteral("Cool Gamer says: hello everyone"));

        // The broadcaster's /me, a VIP, an ignored bot and blocked words.
        twitch.send(QStringLiteral("@badges=broadcaster/1;display-name=SomeChannel :somechannel!somechannel@somechannel"
                                   ".tmi.twitch.tv PRIVMSG #somechannel :\x01" "ACTION waves\x01\r\n"));
        twitch.send(QStringLiteral("@badges=vip/1;display-name=Vippy;vip=1 :vippy!vippy@vippy.tmi.twitch.tv PRIVMSG "
                                   "#somechannel :no spoilers please\r\n"
                                   "@display-name=Nightbot :nightbot!nightbot@nightbot.tmi.twitch.tv PRIVMSG "
                                   "#somechannel :Follow the rules\r\n"
                                   "@display-name=Troll :troll!troll@troll.tmi.twitch.tv PRIVMSG #somechannel "
                                   ":big SPOILER: he dies\r\n"));
        QTRY_COMPARE(messages.size(), 6);
        const auto action = messages.at(2).at(0).value<TwitchChat::Message>();
        QVERIFY(action.broadcaster);
        QVERIFY(action.action);
        QCOMPARE(action.text, QStringLiteral("waves"));
        QVERIFY(messages.at(3).at(0).value<TwitchChat::Message>().vip);
        QCOMPARE(speak.size(), 3);
        QCOMPARE(speak.at(1).at(0).toString(), QStringLiteral("SomeChannel waves"));
        QCOMPARE(speak.at(2).at(0).toString(), QStringLiteral("Vippy says: no spoilers please"));

        // Twitch restarts its server: the client comes back and logs in again.
        twitch.send(QStringLiteral(":tmi.twitch.tv RECONNECT\r\n"));
        QTRY_COMPARE(twitch.connections, 2);
        QTRY_COMPARE(twitch.count(QStringLiteral("PASS SCHMOOPIIE")), 2);
        QTRY_COMPARE(twitch.count(QStringLiteral("JOIN #somechannel")), 2);
        QVERIFY(!chat.isConnected());
        twitch.send(QStringLiteral(":%1!%1@%1.tmi.twitch.tv JOIN #somechannel\r\n").arg(twitch.nick()));
        QTRY_VERIFY(chat.isConnected());

        // The connection drops: retry after the back-off delay.
        twitch.clients.last()->close();
        QTRY_VERIFY(!chat.isConnected());
        QTRY_VERIFY(status.last().at(1).toString().startsWith(QStringLiteral("Couldn't reach Twitch chat")));
        QTRY_COMPARE(twitch.connections, 3);
        QTRY_COMPARE(twitch.count(QStringLiteral("JOIN #somechannel")), 3);

        // Leaving stops reconnecting.
        chat.disconnectFrom();
        QCOMPARE(status.last().at(0).toBool(), false);
        QTest::qWait(300);
        QCOMPARE(twitch.connections, 3);
    }

    void backsOffWhenUnreachable()
    {
        int port = 0;
        {
            FakeTwitch gone;
            QVERIFY(gone.listen());
            port = QUrl(gone.url()).port();
            gone.close();
        }
        TwitchChat chat;
        chat.setServerUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port));
        chat.setReconnectDelays(20, 40);
        QSignalSpy status(&chat, &TwitchChat::statusChanged);
        chat.connectTo(QStringLiteral("somebody"));
        // Connecting, failed, connecting again... (a refused connection can take a while on Windows).
        QTRY_VERIFY_WITH_TIMEOUT(status.size() >= 3, 20000);
        bool sawFailure = false;
        for (const QList<QVariant> &s : std::as_const(status)) {
            QVERIFY(!s.at(0).toBool());
            sawFailure = sawFailure || s.at(1).toString().startsWith(QStringLiteral("Couldn't reach Twitch chat"));
        }
        QVERIFY(sawFailure);
        QVERIFY(!chat.isConnected());
        chat.disconnectFrom();
    }

    void rejectsBadChannelNames()
    {
        TwitchChat chat;
        chat.setServerUrl(QStringLiteral("ws://127.0.0.1:1"));
        QSignalSpy status(&chat, &TwitchChat::statusChanged);
        chat.connectTo(QStringLiteral("not a channel!"));
        QCOMPARE(status.size(), 1);
        QCOMPARE(status.first().at(0).toBool(), false);
        QVERIFY(!chat.isConnected());

        chat.connectTo(QStringLiteral("https://www.twitch.tv/Some_Streamer"));
        QCOMPARE(chat.channel(), QStringLiteral("some_streamer"));
        chat.disconnectFrom();
    }

    void filtersMessages()
    {
        TwitchChat::Filter f;
        f.readUserName = false;
        const auto speech = [&f](const TwitchChat::Message &m) { return TwitchChat::speechFor(m, f); };
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("  hello   there "))), QStringLiteral("hello there"));
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("   "))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("!discord"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("see https://x.com/y"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("join discord.gg/abc"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("i.e. no link here"))), QStringLiteral("i.e. no link here"));
        f.skipCommands = false;
        f.skipLinks = false;
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("!discord"))), QStringLiteral("!discord"));
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("www.x.com"))), QStringLiteral("www.x.com"));

        // Ignored users and blocked words, case-insensitive; blocked words match whole words only.
        f.ignoredUsers = {QStringLiteral("NIGHTBOT"), QStringLiteral(" @StreamElements ")};
        f.blockedWords = {QStringLiteral("Spoiler"), QStringLiteral("bad phrase"), QStringLiteral("")};
        QCOMPARE(speech(chatter(QStringLiteral("nightbot"), QStringLiteral("hi"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("streamelements"), QStringLiteral("hi"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("SPOILER alert"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("what a Bad  Phrase"))), QString());
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("spoilers are fine"))), QStringLiteral("spoilers are fine"));
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("nospoiler"))), QStringLiteral("nospoiler"));

        // Who may talk.
        TwitchChat::Message sub = chatter(QStringLiteral("s"), QStringLiteral("hi"));
        sub.subscriber = true;
        TwitchChat::Message mod = chatter(QStringLiteral("m"), QStringLiteral("hi"));
        mod.moderator = true;
        TwitchChat::Message owner = chatter(QStringLiteral("o"), QStringLiteral("hi"));
        owner.broadcaster = true;
        const TwitchChat::Message viewer = chatter(QStringLiteral("v"), QStringLiteral("hi"));
        f.subscribersOnly = true;
        QCOMPARE(speech(viewer), QString());
        QCOMPARE(speech(sub), QStringLiteral("hi"));
        QCOMPARE(speech(mod), QStringLiteral("hi"));
        QCOMPARE(speech(owner), QStringLiteral("hi"));
        f.subscribersOnly = false;
        f.moderatorsOnly = true;
        QCOMPARE(speech(sub), QString());
        QCOMPARE(speech(mod), QStringLiteral("hi"));
        QCOMPARE(speech(owner), QStringLiteral("hi"));
        f.moderatorsOnly = false;

        // Emote spam is read once; normal repetition is left alone.
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("LUL LUL LUL LUL LUL LUL that was great LUL"))),
                 QStringLiteral("LUL that was great"));
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("go go go go"))), QStringLiteral("go go go go"));

        // Long messages are cut at a word with an ellipsis.
        f.maxLength = 20;
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("this message is far too long to read"))),
                 QStringLiteral(u"this message is far…"));
        QCOMPARE(speech(chatter(QStringLiteral("a"), QStringLiteral("short one"))), QStringLiteral("short one"));
        f.maxLength = 0;

        // Names.
        f.readUserName = true;
        TwitchChat::Message named = chatter(QStringLiteral("xx_gamer_xx"), QStringLiteral("gg"));
        named.displayName = QStringLiteral("xX_Gamer_Xx");
        QCOMPARE(speech(named), QStringLiteral("xX Gamer Xx says: gg"));
        named.action = true;
        QCOMPARE(speech(named), QStringLiteral("xX Gamer Xx gg"));
    }
};

QTEST_GUILESS_MAIN(TestTwitchChat)
#include "test_twitchchat.moc"
