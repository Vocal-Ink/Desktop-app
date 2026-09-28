#include "obs/OverlayServer.h"

#include "Version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>
#include <cmath>
#include <utility>

static void initOverlayResources()
{
    // core.qrc is compiled into a static library; referencing it keeps the linker from dropping it.
    Q_INIT_RESOURCE(core);
}

namespace {
constexpr qint64 kMaxHeaderBytes = 16 * 1024;
constexpr int kRequestTimeoutMs = 10000;
const char kRequestTimerName[] = "overlayRequestTimer";
constexpr int kProgressIntervalMs = 66; // ~15 Hz
constexpr int kLevelIntervalMs = 50;    // 20 Hz
constexpr double kLevelDeadBand = 0.02;
constexpr qint64 kMaxIncomingBytes = 1024;
constexpr qint64 kChatLinkWindowMs = 10 * 60 * 1000; // a chat message waits this long to be read

struct HttpRequest
{
    QByteArray method;
    QByteArray path;
    QByteArray query;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    QByteArray header(const char *name) const { return headers.value(QByteArray(name)); }
};

struct Response
{
    int status = 404;
    QByteArray contentType = QByteArrayLiteral("text/plain; charset=utf-8");
    QByteArray body = QByteArrayLiteral("Not found");
    QList<QPair<QByteArray, QByteArray>> headers;
};

bool parseRequest(const QByteArray &head, HttpRequest &request)
{
    const QList<QByteArray> lines = head.split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(1).startsWith('/'))
        return false;
    request.method = requestLine.at(0);
    const QByteArray target = requestLine.at(1);
    const qsizetype query = target.indexOf('?');
    request.path = query < 0 ? target : target.left(query);
    request.query = query < 0 ? QByteArray() : target.mid(query + 1);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    return true;
}

bool hasToken(const QByteArray &headerValue, const char *token)
{
    const QList<QByteArray> parts = headerValue.split(',');
    for (const QByteArray &part : parts) {
        if (part.trimmed().compare(token, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return QByteArrayLiteral("OK");
    case 400: return QByteArrayLiteral("Bad Request");
    case 403: return QByteArrayLiteral("Forbidden");
    case 404: return QByteArrayLiteral("Not Found");
    case 405: return QByteArrayLiteral("Method Not Allowed");
    case 431: return QByteArrayLiteral("Request Header Fields Too Large");
    default: return QByteArrayLiteral("Error");
    }
}

// "127.0.0.1:7342" -> "127.0.0.1", "[::1]:7342" -> "::1", "Localhost.:80" -> "localhost"
QString hostName(const QByteArray &hostHeader)
{
    QString host = QString::fromLatin1(hostHeader).trimmed().toLower();
    if (host.startsWith(QLatin1Char('['))) {
        const qsizetype end = host.indexOf(QLatin1Char(']'));
        return end < 0 ? host : host.mid(1, end - 1);
    }
    const qsizetype colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0)
        host = host.left(colon);
    while (host.endsWith(QLatin1Char('.')))
        host.chop(1);
    return host;
}

bool isLoopbackName(const QString &host)
{
    if (host == QLatin1String("localhost"))
        return true;
    const QHostAddress address(host);
    return !address.isNull() && address.isLoopback();
}

QByteArray toJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray resource(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray textResponseType()
{
    return QByteArrayLiteral("text/plain; charset=utf-8");
}

// Lower-case words (letters and digits) for matching chat messages to captions.
QStringList wordsOf(const QString &text, int max = -1)
{
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QStringList words = text.toLower().split(separators, Qt::SkipEmptyParts);
    if (max >= 0 && words.size() > max)
        words = words.mid(0, max);
    return words;
}

// Most of the chat message's first words appear in the spoken caption
// (which may add "<name> says:", read emoji as words or cut long messages).
bool readsChat(const QStringList &captionWords, const QStringList &chatWords)
{
    if (chatWords.isEmpty())
        return false;
    int found = 0;
    for (const QString &w : chatWords)
        found += captionWords.contains(w) ? 1 : 0;
    return found * 10 >= int(chatWords.size()) * 6;
}

QString cleanText(const QString &text, int max)
{
    QString out;
    out.reserve(qMin(text.size(), qsizetype(max)));
    for (const QChar c : text) {
        if (out.size() >= max)
            break;
        out += c.unicode() < 0x20 || c.unicode() == 0x7f ? QChar(QLatin1Char(' ')) : c;
    }
    return out.trimmed();
}

double rounded(double value, double scale)
{
    return std::round(value * scale) / scale;
}

// Family and weight from bundled file names: "AtkinsonHyperlegibleNext-SemiBold.ttf".
struct FontFace
{
    QString family;
    int weight = 400;
    bool italic = false;
};

FontFace fontFaceFor(const QString &fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName();
    if (base == QLatin1String("BricolageGrotesque-Display"))
        return {QStringLiteral("Vocal Ink Display"), 800, false}; // resources/fonts/LICENSES.md
    const QString familyPart = base.section(QLatin1Char('-'), 0, 0);
    const QString stylePart = base.section(QLatin1Char('-'), 1).toLower();
    static const QHash<QString, QString> known = {
        {QStringLiteral("BricolageGrotesque"), QStringLiteral("Bricolage Grotesque")},
        {QStringLiteral("AtkinsonHyperlegibleNext"), QStringLiteral("Atkinson Hyperlegible Next")},
        {QStringLiteral("AtkinsonHyperlegibleMono"), QStringLiteral("Atkinson Hyperlegible Mono")},
        {QStringLiteral("Lexend"), QStringLiteral("Lexend")},
        {QStringLiteral("OpenDyslexic"), QStringLiteral("OpenDyslexic")},
    };
    FontFace face;
    face.family = known.value(familyPart);
    if (face.family.isEmpty()) {
        static const QRegularExpression camel(QStringLiteral("(?<=[a-z])(?=[A-Z])"));
        face.family = QString(familyPart).replace(camel, QStringLiteral(" "));
    }
    static const QList<QPair<QString, int>> weights = {
        {QStringLiteral("extralight"), 200}, {QStringLiteral("ultralight"), 200}, {QStringLiteral("semibold"), 600},
        {QStringLiteral("demibold"), 600},   {QStringLiteral("extrabold"), 800},  {QStringLiteral("ultrabold"), 800},
        {QStringLiteral("thin"), 100},       {QStringLiteral("light"), 300},      {QStringLiteral("medium"), 500},
        {QStringLiteral("bold"), 700},       {QStringLiteral("black"), 900},      {QStringLiteral("heavy"), 900},
    };
    for (const auto &w : weights) {
        if (stylePart.contains(w.first)) {
            face.weight = w.second;
            break;
        }
    }
    face.italic = stylePart.contains(QLatin1String("italic"));
    return face;
}

QByteArray fontContentType(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == QLatin1String("ttf"))
        return QByteArrayLiteral("font/ttf");
    if (ext == QLatin1String("otf"))
        return QByteArrayLiteral("font/otf");
    if (ext == QLatin1String("woff"))
        return QByteArrayLiteral("font/woff");
    if (ext == QLatin1String("woff2"))
        return QByteArrayLiteral("font/woff2");
    return {};
}

QByteArray imageContentType(const QString &format)
{
    if (format == QLatin1String("png"))
        return QByteArrayLiteral("image/png");
    if (format == QLatin1String("gif"))
        return QByteArrayLiteral("image/gif");
    if (format == QLatin1String("webp"))
        return QByteArrayLiteral("image/webp");
    if (format == QLatin1String("jpg"))
        return QByteArrayLiteral("image/jpeg");
    return {};
}

// Only characters a (validated) Host header can legitimately contain, for the CSP.
QByteArray cspHost(const QByteArray &host)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9.:\\-\\[\\]]{1,255}$"));
    return safe.match(QString::fromLatin1(host)).hasMatch() ? host : QByteArray();
}

QJsonObject makeProfileObject(const OverlayProfile &p)
{
    return {{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("kind"), p.kind}};
}
} // namespace

OverlayServer::OverlayServer(QObject *parent)
    : QObject(parent)
{
    static const bool resourcesReady = [] {
        initOverlayResources();
        return true;
    }();
    Q_UNUSED(resourcesReady)
    m_clock.start();

    // Pages reload themselves when this changes (a new version, or new page files).
    QCryptographicHash pageHash(QCryptographicHash::Sha1);
    for (const char *file : {":/overlay/index.html", ":/overlay/overlay.js", ":/overlay/overlay.css"})
        pageHash.addData(resource(QString::fromLatin1(file)));
    m_build = QStringLiteral(VOCALINK_VERSION "+") + QString::fromLatin1(pageHash.result().toHex().left(8));

    m_progressTimer = new QTimer(this);
    m_progressTimer->setSingleShot(true);
    connect(m_progressTimer, &QTimer::timeout, this, &OverlayServer::flushProgress);
    m_levelTimer = new QTimer(this);
    m_levelTimer->setSingleShot(true);
    connect(m_levelTimer, &QTimer::timeout, this, &OverlayServer::flushLevel);
    loadFonts();
}

OverlayServer::~OverlayServer()
{
    blockSignals(true);
    stop();
}

bool OverlayServer::start(quint16 port, bool allowLan)
{
    stop();
    m_error.clear();
    m_allowLan = allowLan;
    m_ownHostNames.clear();
    if (allowLan) {
        const QString local = QHostInfo::localHostName().toLower();
        const QString domain = QHostInfo::localDomainName().toLower();
        if (!local.isEmpty()) {
            m_ownHostNames << local << local + QStringLiteral(".local");
            if (!domain.isEmpty())
                m_ownHostNames << local + QLatin1Char('.') + domain;
        }
    }
    if (!m_http) {
        m_http = new QTcpServer(this);
        m_http->setProxy(QNetworkProxy::NoProxy);
        connect(m_http, &QTcpServer::newConnection, this, &OverlayServer::onNewConnection);
    }
    if (!m_ws) {
        // Never listens itself: upgrade requests arrive through the HTTP port.
        m_ws = new QWebSocketServer(QStringLiteral("Vocal Ink"), QWebSocketServer::NonSecureMode, this);
        connect(m_ws, &QWebSocketServer::newConnection, this, &OverlayServer::onWebSocketConnection);
    }
    const QHostAddress address(allowLan ? QHostAddress::Any : QHostAddress::LocalHost);
    if (m_http->listen(address, port))
        return true;
    if (m_http->serverError() == QAbstractSocket::AddressInUseError)
        m_error = tr("Port %1 is already in use by another program. Choose a different overlay port.").arg(port);
    else
        m_error = m_http->errorString();
    return false;
}

void OverlayServer::stop()
{
    if (m_http)
        m_http->close();
    const QList<Client> clients = std::exchange(m_clients, {});
    bool any = false;
    for (const Client &c : clients) {
        if (!c.socket)
            continue;
        any = true;
        c.socket->disconnect(this);
        c.socket->close(QWebSocketProtocol::CloseCodeGoingAway);
        c.socket->deleteLater();
    }
    if (any)
        emit clientCountChanged(0);
}

bool OverlayServer::isRunning() const
{
    return m_http && m_http->isListening();
}

quint16 OverlayServer::port() const
{
    return isRunning() ? m_http->serverPort() : 0;
}

QUrl OverlayServer::overlayUrl(const QString &query) const
{
    if (!isRunning())
        return {};
    QUrl url(QStringLiteral("http://127.0.0.1:%1/").arg(port()));
    QString q = query.trimmed();
    if (q.startsWith(QLatin1Char('?')))
        q.remove(0, 1);
    if (!q.isEmpty())
        url.setQuery(q);
    return url;
}

// --- Configuration -------------------------------------------------------------

void OverlayServer::setProfiles(const QList<OverlayProfile> &resolved)
{
    m_profiles = resolved;
    refreshClients();
}

void OverlayServer::setStyle(const QString &profileId, const QJsonObject &resolvedStyle)
{
    bool changed = false;
    for (OverlayProfile &p : m_profiles) {
        if (p.id == profileId && p.style != resolvedStyle) {
            p.style = resolvedStyle;
            changed = true;
        }
    }
    if (changed)
        refreshClients();
}

QUrl OverlayServer::profileUrl(const QString &profileId) const
{
    return overlayUrl(profileId.isEmpty() || profileId == QLatin1String("main")
                          ? QString()
                          : QStringLiteral("profile=") + profileId);
}

void OverlayServer::setLegacyQuery(const QString &query)
{
    if (m_legacyQuery == query)
        return;
    m_legacyQuery = query;
    refreshClients();
}

void OverlayServer::setAssets(const QHash<QString, QString> &assetIdToPath)
{
    m_assets.clear();
    for (auto it = assetIdToPath.begin(); it != assetIdToPath.end(); ++it) {
        if (OverlayStyle::isAssetId(it.key()))
            m_assets.insert(it.key(), it.value());
    }
}

void OverlayServer::setFontDir(const QString &dir)
{
    if (m_fontDir == dir)
        return;
    m_fontDir = dir;
    loadFonts();
    refreshClients();
}

void OverlayServer::setAllowedHosts(const QStringList &hostNames)
{
    m_allowedHosts.clear();
    for (const QString &name : hostNames) {
        QString host = name.trimmed().toLower();
        while (host.endsWith(QLatin1Char('.')))
            host.chop(1);
        if (!host.isEmpty() && !m_allowedHosts.contains(host))
            m_allowedHosts << host;
    }
}

void OverlayServer::setLabels(const QJsonObject &labels)
{
    if (m_labels == labels)
        return;
    m_labels = labels;
    refreshClients();
}

void OverlayServer::loadFonts()
{
    m_fontFiles.clear();
    m_fonts = QJsonArray();
    const QDir dir(m_fontDir);
    QStringList files = dir.entryList(QDir::Files);
    files.sort();
    for (const QString &file : std::as_const(files)) {
        if (fontContentType(file).isEmpty())
            continue;
        m_fontFiles.insert(file, dir.filePath(file));
        const FontFace face = fontFaceFor(file);
        m_fonts.append(QJsonObject{{QStringLiteral("family"), face.family},
                                   {QStringLiteral("url"), QStringLiteral("/fonts/") + file},
                                   {QStringLiteral("weight"), face.weight},
                                   {QStringLiteral("style"), face.italic ? QStringLiteral("italic") : QStringLiteral("normal")}});
    }
}

OverlayProfile OverlayServer::profileFor(const QString &requested) const
{
    const QString id = requested.isEmpty() ? QStringLiteral("main") : requested;
    for (const OverlayProfile &p : m_profiles) {
        if (p.id == id)
            return p;
    }
    for (const OverlayProfile &p : m_profiles) {
        if (p.id == QLatin1String("main"))
            return p;
    }
    if (!m_profiles.isEmpty())
        return m_profiles.first();
    // Not configured yet: the default captions look.
    return {QStringLiteral("main"), tr("Captions"), QStringLiteral("captions"),
            OverlayStyle::effective({}, QStringLiteral("captions"))};
}

QByteArray OverlayServer::configJson(const OverlayProfile &profile) const
{
    QJsonObject presets;
    for (const QString &name : OverlayStyle::presetNames())
        presets.insert(name, OverlayStyle::presetForKind(name, profile.kind));
    return toJson({{QStringLiteral("type"), QStringLiteral("config")},
                   {QStringLiteral("profile"), makeProfileObject(profile)},
                   {QStringLiteral("style"), profile.style},
                   {QStringLiteral("presets"), presets},
                   {QStringLiteral("legacyQuery"), m_legacyQuery},
                   {QStringLiteral("fonts"), m_fonts},
                   {QStringLiteral("labels"), m_labels}});
}

void OverlayServer::updateClient(Client &client)
{
    const OverlayProfile p = profileFor(client.requested);
    const bool kindChanged = !client.kind.isEmpty() && client.kind != p.kind;
    client.kind = p.kind;
    const QJsonObject indicator = p.style.value(QStringLiteral("indicator")).toObject();
    client.wantsLevel = p.kind == QLatin1String("avatar")
        || indicator.value(QStringLiteral("style")).toString() == QLatin1String("wave");
    const QByteArray config = configJson(p);
    if (config == client.lastConfig)
        return;
    client.lastConfig = config;
    if (!client.socket)
        return;
    client.socket->sendTextMessage(QString::fromUtf8(config));
    if (kindChanged)
        sendReplay(client); // the page rebuilt itself as another kind
}

// Everything a page of this kind needs to show the current state, in the
// documented order (after hello and config).
void OverlayServer::sendReplay(const Client &client)
{
    QWebSocket *ws = client.socket;
    if (!ws)
        return;
    const bool captions = client.kind == QLatin1String("captions") || client.kind == QLatin1String("chat");
    if (captions && !m_lastCaption.isEmpty())
        ws->sendTextMessage(QString::fromUtf8(m_lastCaption));
    ws->sendTextMessage(QString::fromUtf8(
        toJson({{QStringLiteral("type"), QStringLiteral("speaking")}, {QStringLiteral("value"), m_speaking}})));
    ws->sendTextMessage(QString::fromUtf8(
        toJson({{QStringLiteral("type"), QStringLiteral("listening")}, {QStringLiteral("value"), m_listening}})));
    ws->sendTextMessage(QString::fromUtf8(
        toJson({{QStringLiteral("type"), QStringLiteral("mic")}, {QStringLiteral("live"), m_micLive}})));
    if (client.kind == QLatin1String("avatar"))
        ws->sendTextMessage(QString::fromUtf8(avatarJson()));
    if (client.kind == QLatin1String("chat")) {
        const qint64 now = m_clock.elapsed();
        for (const ChatEntry &e : std::as_const(m_chat)) {
            QJsonObject replay = e.message;
            replay.insert(QStringLiteral("age"), now - e.receivedMs);
            ws->sendTextMessage(QString::fromUtf8(toJson(replay)));
        }
    }
}

void OverlayServer::refreshClients()
{
    m_clients.removeIf([](const Client &c) { return c.socket.isNull(); });
    for (Client &c : m_clients)
        updateClient(c);
}

QByteArray OverlayServer::avatarJson() const
{
    return toJson({{QStringLiteral("type"), QStringLiteral("avatar")},
                   {QStringLiteral("talking"), m_talking},
                   {QStringLiteral("mic"), m_micLive}});
}

// --- Events --------------------------------------------------------------------

void OverlayServer::showCaption(quint64 id, const QString &text, const QString &voiceName)
{
    m_lastCaptionId = id;
    m_lastCaptionWords = wordsOf(text);
    m_lastCaptionChat.clear();
    m_progressPending.clear();
    m_progressTimer->stop();

    // Is this a chat message being read? Older ones still waiting were skipped.
    const qint64 now = m_clock.elapsed();
    m_chatAwaiting.removeIf([now](const AwaitingChat &a) { return now - a.receivedMs > kChatLinkWindowMs; });
    for (qsizetype i = 0; i < m_chatAwaiting.size(); ++i) {
        if (readsChat(m_lastCaptionWords, m_chatAwaiting.at(i).words)) {
            m_lastCaptionChat = m_chatAwaiting.at(i).id;
            m_chatAwaiting.remove(0, i + 1);
            break;
        }
    }

    QJsonObject caption{{QStringLiteral("type"), QStringLiteral("caption")},
                        {QStringLiteral("id"), qint64(id)},
                        {QStringLiteral("text"), text},
                        {QStringLiteral("voice"), voiceName}};
    if (!m_lastCaptionChat.isEmpty())
        caption.insert(QStringLiteral("chatId"), m_lastCaptionChat);
    m_lastCaption = toJson(caption);
    sendTo(CaptionPages | ChatPages, m_lastCaption);
}

void OverlayServer::sendProgress(quint64 id, double fraction, qint64 playedMs, qint64 totalMs, bool totalKnown)
{
    const double f = std::isfinite(fraction) ? qBound(0.0, fraction, 1.0) : 0.0;
    m_progressPending = toJson({{QStringLiteral("type"), QStringLiteral("progress")},
                                {QStringLiteral("id"), qint64(id)},
                                {QStringLiteral("f"), rounded(f, 10000.0)},
                                {QStringLiteral("ms"), playedMs},
                                {QStringLiteral("total"), totalMs},
                                {QStringLiteral("known"), totalKnown}});
    const bool newCaption = id != m_progressId;
    m_progressId = id;
    const qint64 since = m_clock.elapsed() - m_progressSentMs;
    if (newCaption || m_progressSentMs < 0 || since >= kProgressIntervalMs)
        flushProgress();
    else if (!m_progressTimer->isActive())
        m_progressTimer->start(int(kProgressIntervalMs - since));
}

void OverlayServer::flushProgress()
{
    m_progressTimer->stop();
    if (m_progressPending.isEmpty())
        return;
    sendTo(CaptionPages | ChatPages, std::exchange(m_progressPending, {}));
    m_progressSentMs = m_clock.elapsed();
}

void OverlayServer::sendLevel(double mouth, const QString &viseme)
{
    double v = std::isfinite(mouth) ? qBound(0.0, mouth, 1.0) : 0.0;
    if (v < 0.001)
        v = 0.0;
    m_levelPending = v;
    m_visemePending = v > 0.0 ? viseme : QString();
    m_levelHasPending = true;
    flushLevel();
}

void OverlayServer::flushLevel()
{
    if (!m_levelHasPending)
        return;
    const bool closing = m_levelPending == 0.0 && m_levelSent != 0.0;
    const bool moved = std::abs(m_levelPending - m_levelSent) >= kLevelDeadBand
        || (m_levelPending > 0.0 && m_visemePending != m_visemeSent);
    if (!closing && !moved) {
        m_levelHasPending = false; // inside the dead band
        return;
    }
    const qint64 since = m_clock.elapsed() - m_levelSentMs;
    if (m_levelSentMs >= 0 && since < kLevelIntervalMs) {
        if (!m_levelTimer->isActive())
            m_levelTimer->start(int(kLevelIntervalMs - since));
        return;
    }
    m_levelTimer->stop();
    m_levelHasPending = false;
    m_levelSent = m_levelPending;
    m_visemeSent = m_visemePending;
    m_levelSentMs = m_clock.elapsed();
    sendTo(LevelPages, toJson({{QStringLiteral("type"), QStringLiteral("level")},
                               {QStringLiteral("v"), rounded(m_levelSent, 1000.0)},
                               {QStringLiteral("viseme"), m_visemeSent}}));
}

void OverlayServer::setTalking(bool talking)
{
    if (m_talking == talking)
        return;
    m_talking = talking;
    sendTo(AvatarPages, avatarJson());
}

void OverlayServer::setMicLive(bool live)
{
    if (m_micLive == live)
        return;
    m_micLive = live;
    sendTo(AllPages, toJson({{QStringLiteral("type"), QStringLiteral("mic")}, {QStringLiteral("live"), live}}));
    sendTo(AvatarPages, avatarJson());
}

void OverlayServer::chatMessage(const QJsonObject &message)
{
    // Chat is untrusted: every field is checked and capped before it goes out.
    static const QRegularExpression idChars(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    static const QRegularExpression loginChars(QStringLiteral("^[a-z0-9_]{1,25}$"));
    static const QRegularExpression badgeChars(QStringLiteral("^[a-z0-9_-]{1,24}$"));

    QString id = message.value(QStringLiteral("id")).toString();
    if (!idChars.match(id).hasMatch())
        id = QStringLiteral("local-%1").arg(++m_chatCounter);
    QString login = message.value(QStringLiteral("login")).toString().trimmed().toLower();
    if (!loginChars.match(login).hasMatch())
        login.clear();
    QString name = cleanText(message.value(QStringLiteral("name")).toString(), 64);
    if (name.isEmpty())
        name = login;
    const QString rawColor = message.value(QStringLiteral("color")).toString().trimmed();
    static const QRegularExpression hex6(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const QString color = hex6.match(rawColor).hasMatch() ? rawColor.toLower() : QString();
    QJsonArray badges;
    const QJsonArray rawBadges = message.value(QStringLiteral("badges")).toArray();
    for (const QJsonValue &b : rawBadges) {
        const QString badge = b.toString().trimmed().toLower();
        if (badgeChars.match(badge).hasMatch() && badges.size() < 6 && !badges.contains(badge))
            badges.append(badge);
    }
    const QString text = cleanText(message.value(QStringLiteral("text")).toString(), 500);

    QJsonObject chat{{QStringLiteral("type"), QStringLiteral("chat")},
                     {QStringLiteral("id"), id},
                     {QStringLiteral("login"), login},
                     {QStringLiteral("name"), name},
                     {QStringLiteral("color"), color},
                     {QStringLiteral("text"), text},
                     {QStringLiteral("badges"), badges},
                     {QStringLiteral("action"), message.value(QStringLiteral("action")).toBool()}};

    // The app queues the speech first, so reading may already have started.
    const QStringList words = wordsOf(text, 12);
    const qint64 now = m_clock.elapsed();
    if (!m_lastCaption.isEmpty() && m_lastCaptionChat.isEmpty() && readsChat(m_lastCaptionWords, words)) {
        m_lastCaptionChat = id;
        chat.insert(QStringLiteral("captionId"), qint64(m_lastCaptionId));
        QJsonObject caption = QJsonDocument::fromJson(m_lastCaption).object();
        caption.insert(QStringLiteral("chatId"), id);
        m_lastCaption = toJson(caption);
    } else {
        m_chatAwaiting.append({id, words, now});
        while (m_chatAwaiting.size() > 50)
            m_chatAwaiting.removeFirst();
    }

    m_chat.append({chat, now});
    while (m_chat.size() > kChatReplay)
        m_chat.removeFirst();
    sendTo(ChatPages, toJson(chat));
}

void OverlayServer::chatDelete(const QString &messageId)
{
    if (messageId.isEmpty())
        return;
    m_chat.removeIf([&](const ChatEntry &e) { return e.message.value(QStringLiteral("id")).toString() == messageId; });
    m_chatAwaiting.removeIf([&](const AwaitingChat &a) { return a.id == messageId; });
    sendTo(ChatPages, toJson({{QStringLiteral("type"), QStringLiteral("chatDelete")}, {QStringLiteral("id"), messageId}}));
}

void OverlayServer::chatClearUser(const QString &login)
{
    const QString who = login.trimmed().toLower();
    if (who.isEmpty())
        return;
    QStringList removed;
    m_chat.removeIf([&](const ChatEntry &e) {
        if (e.message.value(QStringLiteral("login")).toString() != who)
            return false;
        removed << e.message.value(QStringLiteral("id")).toString();
        return true;
    });
    m_chatAwaiting.removeIf([&](const AwaitingChat &a) { return removed.contains(a.id); });
    sendTo(ChatPages, toJson({{QStringLiteral("type"), QStringLiteral("chatClearUser")}, {QStringLiteral("login"), who}}));
}

void OverlayServer::chatClear()
{
    m_chat.clear();
    m_chatAwaiting.clear();
    sendTo(ChatPages, toJson({{QStringLiteral("type"), QStringLiteral("chatClear")}}));
}

void OverlayServer::endCaption(quint64 id)
{
    if (id == m_lastCaptionId) {
        m_lastCaption.clear();
        m_lastCaptionChat.clear();
        m_lastCaptionWords.clear();
    }
    if (id == m_progressId) {
        m_progressPending.clear();
        m_progressTimer->stop();
    }
    sendTo(CaptionPages | ChatPages,
           toJson({{QStringLiteral("type"), QStringLiteral("end")}, {QStringLiteral("id"), qint64(id)}}));
}

void OverlayServer::clearCaptions()
{
    m_lastCaption.clear();
    m_lastCaptionChat.clear();
    m_lastCaptionWords.clear();
    m_progressPending.clear();
    m_progressTimer->stop();
    sendTo(CaptionPages | ChatPages, toJson({{QStringLiteral("type"), QStringLiteral("clear")}}));
}

void OverlayServer::setSpeaking(bool speaking)
{
    m_speaking = speaking;
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("speaking")}, {QStringLiteral("value"), speaking}}));
}

void OverlayServer::setListening(bool listening)
{
    m_listening = listening;
    broadcast(toJson({{QStringLiteral("type"), QStringLiteral("listening")}, {QStringLiteral("value"), listening}}));
}

// --- HTTP ----------------------------------------------------------------------

void OverlayServer::onNewConnection()
{
    while (QTcpSocket *socket = m_http->nextPendingConnection()) {
        auto *timer = new QTimer(socket);
        timer->setObjectName(QLatin1String(kRequestTimerName));
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
        timer->start(kRequestTimeoutMs);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleHttp(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [socket] { socket->deleteLater(); });
    }
}

void OverlayServer::handleHttp(QTcpSocket *socket)
{
    // Peek only: a WebSocket upgrade must reach QWebSocketServer untouched.
    const QByteArray buffered = socket->peek(kMaxHeaderBytes);
    const qsizetype headerEnd = buffered.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buffered.size() >= kMaxHeaderBytes)
            respond(socket, 431, textResponseType(), QByteArrayLiteral("Header too large"));
        return;
    }

    HttpRequest request;
    if (!parseRequest(buffered.left(headerEnd), request)) {
        respond(socket, 400, textResponseType(), QByteArrayLiteral("Bad request"));
        return;
    }
    const QByteArray host = request.header("host");
    if (!isAllowedHost(host)) {
        respond(socket, 403, textResponseType(), QByteArrayLiteral("Forbidden"));
        return;
    }

    if (request.path == "/ws" && hasToken(request.header("upgrade"), "websocket")) {
        if (!isAllowedOrigin(request.header("origin"), host)) {
            respond(socket, 403, textResponseType(), QByteArrayLiteral("Forbidden"));
            return;
        }
        socket->disconnect(this);
        delete socket->findChild<QTimer *>(QLatin1String(kRequestTimerName), Qt::FindDirectChildrenOnly);
        socket->setParent(nullptr); // QWebSocketServer takes ownership
        // The handshake is already buffered, so the socket won't signal readyRead
        // again by itself. Qt 6.4+ nudges it in handleConnection(); make sure
        // exactly one nudge happens whatever the Qt version does.
        bool nudged = false;
        const QMetaObject::Connection probe =
            connect(socket, &QTcpSocket::readyRead, socket, [&nudged] { nudged = true; });
        m_ws->handleConnection(socket);
        disconnect(probe);
        if (!nudged)
            emit socket->readyRead();
        return;
    }

    socket->read(headerEnd + 4);
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr); // one request per connection

    const bool head = request.method == "HEAD";
    if (request.method != "GET" && !head) {
        respond(socket, 405, textResponseType(), QByteArrayLiteral("Method not allowed"));
        return;
    }

    // Routes: exact paths, or a prefix followed by one file name. Nothing ever
    // builds a file path from the URL: fonts and assets are looked up in lists.
    using Handler = Response (*)(const OverlayServer *, const QByteArray &rest, const QByteArray &host);
    struct Route
    {
        const char *path;
        bool prefix;
        Handler handler;
    };
    static const Route routes[] = {
        {"/", false,
         [](const OverlayServer *, const QByteArray &, const QByteArray &host) {
             Response r;
             r.status = 200;
             r.contentType = QByteArrayLiteral("text/html; charset=utf-8");
             r.body = resource(QStringLiteral(":/overlay/index.html"));
             const QByteArray h = cspHost(host);
             r.headers = {{QByteArrayLiteral("Content-Security-Policy"),
                           QByteArrayLiteral("default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                                             "font-src 'self' data:; connect-src 'self'")
                               + (h.isEmpty() ? QByteArray() : QByteArrayLiteral(" ws://") + h)
                               + QByteArrayLiteral("; script-src 'self'; object-src 'none'; base-uri 'none'; "
                                                   "form-action 'none'")},
                          {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")},
                          {QByteArrayLiteral("Referrer-Policy"), QByteArrayLiteral("no-referrer")}};
             return r;
         }},
        {"/overlay.js", false,
         [](const OverlayServer *, const QByteArray &, const QByteArray &) {
             return Response{200, QByteArrayLiteral("text/javascript; charset=utf-8"),
                             resource(QStringLiteral(":/overlay/overlay.js")), {}};
         }},
        {"/overlay.css", false,
         [](const OverlayServer *, const QByteArray &, const QByteArray &) {
             return Response{200, QByteArrayLiteral("text/css; charset=utf-8"),
                             resource(QStringLiteral(":/overlay/overlay.css")), {}};
         }},
        {"/state", false,
         [](const OverlayServer *self, const QByteArray &, const QByteArray &) {
             return Response{200, QByteArrayLiteral("application/json"), self->stateJson(), {}};
         }},
        {"/health", false,
         [](const OverlayServer *, const QByteArray &, const QByteArray &) {
             return Response{200, textResponseType(), QByteArrayLiteral("ok"), {}};
         }},
        {"/fonts/", true,
         [](const OverlayServer *self, const QByteArray &rest, const QByteArray &) {
             const QString file = QString::fromLatin1(rest);
             const QString path = self->m_fontFiles.value(file);
             const QByteArray type = fontContentType(file);
             if (path.isEmpty() || type.isEmpty())
                 return Response();
             QFile f(path);
             if (!f.open(QIODevice::ReadOnly) || f.size() > OverlayStyle::kMaxAssetBytes)
                 return Response();
             return Response{200, type, f.readAll(), {}};
         }},
        {"/assets/", true,
         [](const OverlayServer *self, const QByteArray &rest, const QByteArray &) {
             const QString id = QString::fromLatin1(rest);
             if (!OverlayStyle::isAssetId(id))
                 return Response();
             const QString path = self->m_assets.value(id);
             if (path.isEmpty())
                 return Response();
             QFile f(path);
             if (!f.open(QIODevice::ReadOnly) || f.size() > OverlayStyle::kMaxAssetBytes)
                 return Response();
             const QByteArray data = f.read(OverlayStyle::kMaxAssetBytes + 1);
             const QByteArray type = imageContentType(OverlayStyle::imageFormat(data));
             if (data.size() > OverlayStyle::kMaxAssetBytes || type.isEmpty())
                 return Response();
             // Content-addressed: the same id never changes.
             return Response{200, type, data, {{QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("max-age=86400")}}};
         }},
    };

    const QByteArray &path = request.path == "/index.html" ? QByteArrayLiteral("/") : request.path;
    Response response;
    for (const Route &route : routes) {
        const QByteArray routePath(route.path);
        if (!route.prefix && path == routePath) {
            response = route.handler(this, QByteArray(), host);
            break;
        }
        if (route.prefix && path.startsWith(routePath)) {
            const QByteArray rest = path.mid(routePath.size());
            if (!rest.isEmpty() && !rest.contains('/') && !rest.contains('\\') && !rest.contains('%'))
                response = route.handler(this, rest, host);
            break;
        }
    }
    respond(socket, response.status, response.contentType, response.body, head, response.headers);
}

void OverlayServer::respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                            bool headOnly, const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    bool cacheSet = false;
    for (const auto &h : extraHeaders) {
        response += h.first + ": " + h.second + "\r\n";
        cacheSet = cacheSet || h.first.compare("Cache-Control", Qt::CaseInsensitive) == 0;
    }
    if (!cacheSet)
        response += "Cache-Control: no-cache\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    response += "Connection: close\r\n\r\n";
    if (!headOnly)
        response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

// --- WebSocket -------------------------------------------------------------------

void OverlayServer::onWebSocketConnection()
{
    while (QWebSocket *ws = m_ws->nextPendingConnection()) {
        ws->setParent(this);
        m_clients.removeIf([](const Client &c) { return c.socket.isNull(); });
        if (m_clients.size() >= kMaxPages) {
            connect(ws, &QWebSocket::disconnected, ws, &QObject::deleteLater);
            ws->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("Too many overlay pages"));
            QTimer::singleShot(2000, ws, &QObject::deleteLater);
            continue;
        }
        // Pages never send anything that matters: small frames only, ignored.
        ws->setMaxAllowedIncomingFrameSize(kMaxIncomingBytes);
        ws->setMaxAllowedIncomingMessageSize(kMaxIncomingBytes);

        Client client;
        client.socket = ws;
        client.requested = QUrlQuery(ws->requestUrl()).queryItemValue(QStringLiteral("profile"));
        if (!OverlayStyle::isProfileId(client.requested))
            client.requested.clear();
        connect(ws, &QWebSocket::disconnected, this, [this, ws] {
            m_clients.removeIf([ws](const Client &c) { return c.socket == ws || c.socket.isNull(); });
            ws->deleteLater();
            emit clientCountChanged(clientCount());
        });

        // Bring the new page up to date: hello, config, then the current state.
        ws->sendTextMessage(QString::fromUtf8(toJson({{QStringLiteral("type"), QStringLiteral("hello")},
                                                      {QStringLiteral("v"), 2},
                                                      {QStringLiteral("build"), m_build}})));
        updateClient(client); // sends the config
        sendReplay(client);
        m_clients.append(client);
        emit clientCountChanged(clientCount());
    }
}

void OverlayServer::broadcast(const QByteArray &json)
{
    sendTo(AllPages, json);
}

void OverlayServer::sendTo(int pageKinds, const QByteArray &json)
{
    m_clients.removeIf([](const Client &c) { return c.socket.isNull(); });
    const QString message = QString::fromUtf8(json);
    for (const Client &c : std::as_const(m_clients)) {
        int kind = CaptionPages;
        if (c.kind == QLatin1String("chat"))
            kind = ChatPages;
        else if (c.kind == QLatin1String("avatar"))
            kind = AvatarPages;
        const bool wanted = (pageKinds & kind) || ((pageKinds & LevelPages) && c.wantsLevel);
        if (wanted)
            c.socket->sendTextMessage(message);
    }
}

// Only answer requests addressed to this machine by name, so a web page that
// re-points its own domain at this PC (DNS rebinding) can't read the captions.
bool OverlayServer::isAllowedHost(const QByteArray &host) const
{
    if (host.isEmpty())
        return true; // not a browser
    const QString name = hostName(host);
    if (isLoopbackName(name))
        return true;
    if (!m_allowLan)
        return false;
    if (!QHostAddress(name).isNull())
        return true; // an IP address typed into OBS
    return m_ownHostNames.contains(name) || m_allowedHosts.contains(name);
}

// Browsers send an Origin with WebSocket handshakes: accept our own page (any
// host it was loaded from) and local pages, but not arbitrary websites.
bool OverlayServer::isAllowedOrigin(const QByteArray &origin, const QByteArray &host) const
{
    if (origin.isEmpty())
        return true; // not a browser
    const QUrl url(QString::fromLatin1(origin));
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        return false;
    if (url.authority().compare(QString::fromLatin1(host), Qt::CaseInsensitive) == 0)
        return true;
    return isLoopbackName(url.host());
}

QByteArray OverlayServer::stateJson() const
{
    const QJsonDocument caption = QJsonDocument::fromJson(m_lastCaption);
    QJsonArray profiles;
    for (const OverlayProfile &p : m_profiles)
        profiles.append(makeProfileObject(p));
    return toJson({{QStringLiteral("caption"), caption.isObject() ? QJsonValue(caption.object()) : QJsonValue()},
                   {QStringLiteral("speaking"), m_speaking},
                   {QStringLiteral("listening"), m_listening},
                   {QStringLiteral("mic"), m_micLive},
                   {QStringLiteral("talking"), m_talking},
                   {QStringLiteral("profiles"), profiles},
                   {QStringLiteral("clients"), clientCount()}});
}
