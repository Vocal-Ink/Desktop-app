#include "obs/TwitchChat.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <utility>

namespace {

constexpr int kFirstReconnectMs = 2000;
constexpr int kMaxReconnectMs = 30000;
constexpr int kWatchdogMs = 30000;   // how often the connection is checked
constexpr int kKeepAliveMs = 60000;  // quiet this long: send our own PING
constexpr int kDeadAfterMs = 150000; // nothing at all this long: reconnect
constexpr int kSpamRepeats = 4;      // a word used more often than this is read once

struct IrcMessage
{
    QHash<QString, QString> tags;
    QString nick; // from the prefix
    QString command;
    QStringList params;
};

QString unescapeTag(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\')) {
            out += c;
            continue;
        }
        if (++i >= value.size())
            break;
        switch (value.at(i).unicode()) {
        case ':':
            out += QLatin1Char(';');
            break;
        case 's':
            out += QLatin1Char(' ');
            break;
        case 'r':
            out += QLatin1Char('\r');
            break;
        case 'n':
            out += QLatin1Char('\n');
            break;
        default:
            out += value.at(i);
            break;
        }
    }
    return out;
}

// Parses one IRCv3 line: "@tags :prefix COMMAND param param :trailing".
bool parseIrc(const QString &line, IrcMessage *out)
{
    qsizetype pos = 0;
    const qsizetype size = line.size();
    const auto skipSpaces = [&] {
        while (pos < size && line.at(pos) == QLatin1Char(' '))
            ++pos;
    };
    const auto word = [&] {
        const qsizetype start = pos;
        while (pos < size && line.at(pos) != QLatin1Char(' '))
            ++pos;
        return line.mid(start, pos - start);
    };
    skipSpaces();
    if (pos < size && line.at(pos) == QLatin1Char('@')) {
        ++pos;
        const QStringList tags = word().split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &tag : tags) {
            const qsizetype eq = tag.indexOf(QLatin1Char('='));
            if (eq < 0)
                out->tags.insert(tag, QString());
            else
                out->tags.insert(tag.left(eq), unescapeTag(tag.mid(eq + 1)));
        }
        skipSpaces();
    }
    if (pos < size && line.at(pos) == QLatin1Char(':')) {
        ++pos;
        const QString prefix = word();
        const qsizetype bang = prefix.indexOf(QLatin1Char('!'));
        out->nick = bang >= 0 ? prefix.left(bang) : prefix;
        skipSpaces();
    }
    out->command = word().toUpper();
    while (pos < size) {
        skipSpaces();
        if (pos >= size)
            break;
        if (line.at(pos) == QLatin1Char(':')) {
            out->params << line.mid(pos + 1);
            break;
        }
        out->params << word();
    }
    return !out->command.isEmpty();
}

bool containsLink(const QString &text)
{
    static const QRegularExpression link(
        QStringLiteral("(?:https?://|www\\.)\\S+|\\b[\\w-]+(?:\\.[\\w-]+)*\\.(?:com|net|org|tv|gg|io|ly|me|co|xyz|app|"
                       "dev|link|live|gl|be|info|shop|store|site|online)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return link.match(text).hasMatch();
}

bool containsWord(const QString &text, const QString &word)
{
    const QString w = word.trimmed();
    if (w.isEmpty())
        return false;
    const QRegularExpression re(QStringLiteral("(?<![\\p{L}\\p{N}_])") + QRegularExpression::escape(w)
                                    + QStringLiteral("(?![\\p{L}\\p{N}_])"),
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

bool isLoopback(const QUrl &url)
{
    if (url.host().compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress address(url.host());
    return !address.isNull() && address.isLoopback();
}

} // namespace

class TwitchChat::Private
{
public:
    QObject *owner = nullptr;
    QWebSocket *socket = nullptr;
    QTimer *reconnectTimer = nullptr;
    QTimer *watchdog = nullptr;
    QElapsedTimer lastActivity;
    QString serverUrl = QStringLiteral("wss://irc-ws.chat.twitch.tv:443");
    QString nick;
    bool wanted = false;
    bool joined = false;
    int firstDelayMs = kFirstReconnectMs;
    int maxDelayMs = kMaxReconnectMs;
    int delayMs = kFirstReconnectMs;

    void discardSocket()
    {
        if (!socket)
            return;
        QWebSocket *s = socket;
        socket = nullptr;
        s->disconnect(owner);
        s->abort();
        s->deleteLater();
    }

    void send(const QString &line)
    {
        if (socket && socket->state() == QAbstractSocket::ConnectedState)
            socket->sendTextMessage(line);
    }
};

TwitchChat::TwitchChat(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->owner = this;
    d->reconnectTimer = new QTimer(this);
    d->reconnectTimer->setSingleShot(true);
    connect(d->reconnectTimer, &QTimer::timeout, this, [this] { openSocket(); });

    d->watchdog = new QTimer(this);
    d->watchdog->setInterval(kWatchdogMs);
    connect(d->watchdog, &QTimer::timeout, this, [this] {
        if (!d->socket || d->socket->state() != QAbstractSocket::ConnectedState)
            return;
        const qint64 quiet = d->lastActivity.elapsed();
        if (quiet > kDeadAfterMs)
            onSocketClosed(d->socket);
        else if (quiet > kKeepAliveMs)
            d->send(QStringLiteral("PING :tmi.twitch.tv"));
    });
}

TwitchChat::~TwitchChat()
{
    d->wanted = false;
    d->discardSocket();
    delete d;
}

void TwitchChat::connectTo(const QString &channel)
{
    QString name = channel.trimmed();
    static const QRegularExpression fromUrl(QStringLiteral("twitch\\.tv/([A-Za-z0-9_]+)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = fromUrl.match(name);
    if (m.hasMatch())
        name = m.captured(1);
    while (name.startsWith(QLatin1Char('#')) || name.startsWith(QLatin1Char('@')))
        name.remove(0, 1);
    name = name.toLower();

    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{1,25}$"));
    if (!valid.match(name).hasMatch()) {
        disconnectFrom();
        emit statusChanged(false, tr("“%1” isn't a Twitch channel name.").arg(channel.trimmed()));
        return;
    }
    m_channel = name;
    d->wanted = true;
    d->delayMs = d->firstDelayMs;
    d->nick = QStringLiteral("justinfan%1").arg(QRandomGenerator::global()->bounded(10000, 100000));
    d->reconnectTimer->stop();
    openSocket();
}

void TwitchChat::disconnectFrom()
{
    const bool wasActive = d->wanted || d->socket;
    d->wanted = false;
    d->joined = false;
    d->reconnectTimer->stop();
    d->watchdog->stop();
    d->discardSocket();
    if (wasActive)
        emit statusChanged(false, tr("Not reading Twitch chat"));
}

bool TwitchChat::isConnected() const
{
    return d->joined;
}

void TwitchChat::setServerUrl(const QString &url)
{
    d->serverUrl = url;
}

void TwitchChat::setReconnectDelays(int firstMs, int maxMs)
{
    d->firstDelayMs = qMax(10, firstMs);
    d->maxDelayMs = qMax(d->firstDelayMs, maxMs);
    d->delayMs = d->firstDelayMs;
}

void TwitchChat::openSocket()
{
    d->discardSocket();
    d->joined = false;
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    d->socket = socket;
    const QUrl url(d->serverUrl);
    if (isLoopback(url))
        socket->setProxy(QNetworkProxy::NoProxy);

    connect(socket, &QWebSocket::connected, this, [this, socket] {
        if (socket != d->socket)
            return;
        d->lastActivity.start();
        d->watchdog->start();
        d->send(QStringLiteral("CAP REQ :twitch.tv/tags twitch.tv/commands"));
        d->send(QStringLiteral("PASS SCHMOOPIIE"));
        d->send(QStringLiteral("NICK ") + d->nick);
        d->send(QStringLiteral("JOIN #") + m_channel);
    });
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &frame) {
        if (socket != d->socket)
            return;
        d->lastActivity.restart();
        const QStringList lines = frame.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (socket != d->socket)
                return; // a line (RECONNECT, a bad channel) replaced the connection
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty())
                handleLine(trimmed);
        }
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] { onSocketClosed(socket); });
    const auto onError = [this, socket](QAbstractSocket::SocketError) {
        // A refused connection does not always end with disconnected().
        QTimer::singleShot(0, this, [this, socket] {
            if (socket == d->socket && socket->state() == QAbstractSocket::UnconnectedState)
                onSocketClosed(socket);
        });
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QWebSocket::errorOccurred, this, onError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, onError);
#endif

    emit statusChanged(false, tr("Connecting to #%1…").arg(m_channel));
    socket->open(url);
}

void TwitchChat::onSocketClosed(QWebSocket *socket)
{
    if (socket != d->socket)
        return;
    d->joined = false;
    d->watchdog->stop();
    d->discardSocket();
    if (!d->wanted)
        return;
    const int seconds = (d->delayMs + 999) / 1000;
    emit statusChanged(false, tr("Couldn't reach Twitch chat. Trying again in %n second(s)…", nullptr, seconds));
    d->reconnectTimer->start(d->delayMs);
    d->delayMs = qMin(d->delayMs * 2, d->maxDelayMs);
}

void TwitchChat::handleLine(const QString &line)
{
    IrcMessage irc;
    if (!parseIrc(line, &irc))
        return;
    if (irc.command == QLatin1String("PING")) {
        d->send(QStringLiteral("PONG :") + irc.params.value(0, QStringLiteral("tmi.twitch.tv")));
    } else if (irc.command == QLatin1String("RECONNECT")) {
        // Twitch is restarting the server: reconnect right away.
        d->joined = false;
        d->watchdog->stop();
        d->discardSocket();
        d->delayMs = d->firstDelayMs;
        emit statusChanged(false, tr("Twitch asked to reconnect, reconnecting…"));
        d->reconnectTimer->start(0);
    } else if (irc.command == QLatin1String("JOIN") || irc.command == QLatin1String("ROOMSTATE")) {
        const bool ours = irc.command != QLatin1String("JOIN") || irc.nick.compare(d->nick, Qt::CaseInsensitive) == 0;
        if (d->joined || !ours)
            return;
        d->joined = true;
        d->delayMs = d->firstDelayMs;
        emit statusChanged(true, tr("Reading chat from #%1").arg(m_channel));
    } else if (irc.command == QLatin1String("NOTICE")) {
        const QString id = irc.tags.value(QStringLiteral("msg-id"));
        if (id == QLatin1String("msg_channel_suspended") || id == QLatin1String("msg_banned")) {
            d->wanted = false;
            d->joined = false;
            d->watchdog->stop();
            d->discardSocket();
            emit statusChanged(false, tr("#%1 can't be read right now (the channel may be suspended or renamed).")
                                          .arg(m_channel));
        }
    } else if (irc.command == QLatin1String("CLEARMSG")) {
        // A moderator deleted one message: "@login=...;target-msg-id=<id> :tmi.twitch.tv CLEARMSG #chan :text"
        const QString id = irc.tags.value(QStringLiteral("target-msg-id")).trimmed();
        if (!id.isEmpty())
            emit messageDeleted(id);
    } else if (irc.command == QLatin1String("CLEARCHAT")) {
        // Timeout or ban ("CLEARCHAT #chan :login"), or the whole chat ("CLEARCHAT #chan").
        const QString login = irc.params.value(1).trimmed().toLower();
        if (login.isEmpty())
            emit chatCleared();
        else
            emit userCleared(login);
    } else if (irc.command == QLatin1String("PRIVMSG") && irc.params.size() >= 2) {
        Message message;
        message.id = irc.tags.value(QStringLiteral("id")).trimmed();
        message.login = irc.nick.toLower();
        message.displayName = irc.tags.value(QStringLiteral("display-name")).trimmed();
        if (message.displayName.isEmpty())
            message.displayName = irc.nick;
        message.color = irc.tags.value(QStringLiteral("color"));
        QString text = irc.params.at(1);
        static const QString actionStart = QStringLiteral("\x01" "ACTION ");
        if (text.startsWith(actionStart)) {
            message.action = true;
            text = text.mid(actionStart.size());
            if (text.endsWith(QChar(0x01)))
                text.chop(1);
        }
        message.text = text;
        const QStringList badges =
            irc.tags.value(QStringLiteral("badges")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &badge : badges) {
            const QString name = badge.section(QLatin1Char('/'), 0, 0);
            if (name == QLatin1String("broadcaster"))
                message.broadcaster = true;
            else if (name == QLatin1String("moderator"))
                message.moderator = true;
            else if (name == QLatin1String("subscriber") || name == QLatin1String("founder"))
                message.subscriber = true;
            else if (name == QLatin1String("vip"))
                message.vip = true;
        }
        message.moderator = message.moderator || irc.tags.value(QStringLiteral("mod")) == QLatin1String("1");
        message.subscriber = message.subscriber || irc.tags.value(QStringLiteral("subscriber")) == QLatin1String("1");
        message.vip = message.vip || irc.tags.contains(QStringLiteral("vip"));
        message.broadcaster = message.broadcaster || message.login == m_channel;

        emit messageReceived(message);
        const QString speech = speechFor(message, m_filter);
        if (!speech.isEmpty())
            emit speakRequested(speech, message);
    }
}

QString TwitchChat::speechFor(const Message &message, const Filter &filter)
{
    QString text = message.text.simplified();
    if (text.isEmpty())
        return {};

    for (QString user : filter.ignoredUsers) {
        user = user.trimmed();
        while (user.startsWith(QLatin1Char('@')))
            user.remove(0, 1);
        if (!user.isEmpty()
            && (user.compare(message.login, Qt::CaseInsensitive) == 0
                || user.compare(message.displayName, Qt::CaseInsensitive) == 0))
            return {};
    }
    const bool mod = message.moderator || message.broadcaster;
    if (filter.moderatorsOnly && !mod)
        return {};
    if (filter.subscribersOnly && !(message.subscriber || message.vip || mod))
        return {};
    if (filter.skipCommands && text.startsWith(QLatin1Char('!')))
        return {};
    if (filter.skipLinks && containsLink(text))
        return {};
    for (const QString &word : filter.blockedWords) {
        if (containsWord(text, word))
            return {};
    }

    // Emote spam ("LUL LUL LUL LUL LUL LUL"): a word repeated this much is read once.
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QHash<QString, int> counts;
    for (const QString &w : words)
        ++counts[w];
    bool spam = false;
    for (const int c : std::as_const(counts))
        spam = spam || c > kSpamRepeats;
    if (spam) {
        QStringList kept;
        QSet<QString> seen;
        for (const QString &w : words) {
            if (counts.value(w) > kSpamRepeats) {
                if (seen.contains(w))
                    continue;
                seen.insert(w);
            }
            kept << w;
        }
        text = kept.join(QLatin1Char(' '));
    }

    if (filter.maxLength > 0 && text.size() > filter.maxLength) {
        qsizetype cut = filter.maxLength;
        const qsizetype space = text.lastIndexOf(QLatin1Char(' '), cut);
        if (space > filter.maxLength / 2)
            cut = space;
        text = text.left(cut).trimmed();
        while (!text.isEmpty() && QStringLiteral(",;:-").contains(text.at(text.size() - 1)))
            text.chop(1);
        text += QChar(0x2026);
    }

    if (filter.readUserName) {
        QString name = message.displayName.isEmpty() ? message.login : message.displayName;
        name.replace(QLatin1Char('_'), QLatin1Char(' ')); // "cool_gamer_99" reads better as words
        name = name.simplified();
        if (!name.isEmpty())
            text = message.action ? tr("%1 %2").arg(name, text) : tr("%1 says: %2").arg(name, text);
    }
    return text;
}
