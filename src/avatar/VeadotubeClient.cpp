#include "avatar/VeadotubeClient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <algorithm>

namespace {

constexpr int kPollMs = 3000;
constexpr int kConnectTimeoutMs = 4000;
constexpr qint64 kMaxInstanceFileBytes = 64 * 1024;
const QString kClientName = QStringLiteral("Vocal%20Ink"); // mini requires a name

QString jsonString(const QString &s)
{
    const QByteArray a = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}

QString idOf(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 17);
    return QString();
}

// "nodes:{…}" answer to a state list request -> [{id, name}].
bool parseStateList(const QString &message, QVariantList &out)
{
    const qsizetype colon = message.indexOf(QLatin1Char(':'));
    if (colon <= 0 || message.left(colon).trimmed() != QLatin1String("nodes"))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(message.mid(colon + 1).trimmed().toUtf8()).object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (!type.isEmpty() && type != QLatin1String("stateEvents"))
        return false;
    const QJsonObject payload = obj.value(QStringLiteral("payload")).toObject();
    if (payload.value(QStringLiteral("event")).toString() != QLatin1String("list"))
        return false;

    QJsonArray states;
    for (const auto *key : {"states", "list", "value"}) {
        if (payload.value(QLatin1String(key)).isArray()) {
            states = payload.value(QLatin1String(key)).toArray();
            break;
        }
    }
    if (states.isEmpty()) {
        for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
            if (it.value().isArray()) {
                states = it.value().toArray();
                break;
            }
        }
    }
    out.clear();
    for (const QJsonValue &v : std::as_const(states)) {
        QString id;
        QString name;
        if (v.isObject()) {
            const QJsonObject s = v.toObject();
            id = idOf(s.value(QStringLiteral("id")));
            name = s.value(QStringLiteral("name")).toString();
        } else {
            id = idOf(v);
        }
        if (id.isEmpty())
            continue;
        out.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name.isEmpty() ? id : name}});
    }
    return true;
}

} // namespace

VeadotubeClient::VeadotubeClient(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_connectTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    m_pollTimer->setInterval(kPollMs);
    connect(m_pollTimer, &QTimer::timeout, this, &VeadotubeClient::scan);

    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, [this] {
        QWebSocket *socket = m_socket;
        if (!socket || m_opened)
            return;
        m_lastError = QStringLiteral("Timed out connecting to %1").arg(m_server);
        socket->abort();
        if (m_socket == socket)
            onClosed(socket);
    });
}

VeadotubeClient::~VeadotubeClient()
{
    discardSocket();
}

QString VeadotubeClient::defaultInstancesDir()
{
    return QDir::homePath() + QStringLiteral("/.veadotube/instances");
}

QString VeadotubeClient::instancesDir() const
{
    return m_dir.isEmpty() ? defaultInstancesDir() : m_dir;
}

QList<VeadotubeClient::Instance> VeadotubeClient::readInstances(const QString &dir)
{
    struct Found
    {
        Instance instance;
        qint64 modified;
    };
    QList<Found> found;
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : files) {
        if (info.size() <= 0 || info.size() > kMaxInstanceFileBytes)
            continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
        Instance inst;
        inst.file = info.absoluteFilePath();
        inst.server = obj.value(QStringLiteral("server")).toString().trimmed();
        if (inst.server.isEmpty())
            continue;
        inst.id = obj.value(QStringLiteral("id")).toString();
        if (inst.id.isEmpty())
            inst.id = info.fileName();
        inst.name = obj.value(QStringLiteral("name")).toString();
        if (inst.name.isEmpty())
            inst.name = QStringLiteral("veadotube");
        const qint64 modified = info.lastModified().toSecsSinceEpoch();
        inst.time = qint64(obj.value(QStringLiteral("time")).toDouble());
        if (inst.time <= 0)
            inst.time = modified;
        found.append({inst, modified});
    }
    // A running instance keeps refreshing its "time"; left-overs from a crash
    // sink to the end (and are only tried if everything newer fails).
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        if (a.instance.time != b.instance.time)
            return a.instance.time > b.instance.time;
        return a.modified > b.modified;
    });
    QList<Instance> out;
    for (const Found &f : std::as_const(found))
        out.append(f.instance);
    return out;
}

void VeadotubeClient::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    if (!enabled) {
        if (m_pttSent == 1)
            send(pushToTalkMessage(false));
        m_enabled = false;
        discardSocket();
        m_pollTimer->stop();
        m_instanceName.clear();
        setStatus(Status::Off);
        return;
    }
    m_enabled = true;
    setStatus(Status::Searching);
    scan();
}

void VeadotubeClient::setInstancesDirForTesting(const QString &dir)
{
    m_dir = dir;
    if (m_enabled)
        reconnect();
}

void VeadotubeClient::setPollIntervalForTesting(int ms)
{
    m_pollTimer->setInterval(std::max(10, ms));
}

void VeadotubeClient::reconnect()
{
    if (!m_enabled)
        return;
    discardSocket();
    m_pollTimer->stop();
    setStatus(Status::Searching);
    scan();
}

void VeadotubeClient::scan()
{
    if (!m_enabled || m_socket)
        return;
    m_candidates = readInstances(instancesDir());
    m_candidate = -1;
    m_lastError.clear();
    if (m_candidates.isEmpty()) {
        setStatus(Status::NotRunning, QStringLiteral("No veadotube instance in %1").arg(QDir::toNativeSeparators(instancesDir())));
        m_pollTimer->start();
        return;
    }
    tryNext();
}

void VeadotubeClient::tryNext()
{
    if (!m_enabled)
        return;
    ++m_candidate;
    if (m_candidate >= m_candidates.size()) {
        setStatus(Status::NotRunning, m_lastError);
        m_pollTimer->start();
        return;
    }
    openSocket(m_candidates.at(m_candidate));
}

void VeadotubeClient::openSocket(const Instance &instance)
{
    discardSocket();
    ++m_connection;
    m_opened = false;
    m_server = instance.server;

    QUrl url(QStringLiteral("ws://") + instance.server);
    if (!url.isValid() || url.host().isEmpty()) {
        m_lastError = QStringLiteral("Invalid server address \"%1\" in %2").arg(instance.server, instance.file);
        QTimer::singleShot(0, this, &VeadotubeClient::tryNext);
        return;
    }
    url.setQuery(QStringLiteral("n=") + kClientName);

    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket->setProxy(QNetworkProxy::NoProxy); // veadotube is local
    m_socket = socket;
    const quint64 connection = m_connection;
    connect(socket, &QWebSocket::connected, this, [this, socket] { onOpened(socket); });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] { onClosed(socket); });
    connect(socket, &QWebSocket::textMessageReceived, this, &VeadotubeClient::onMessage);
    connect(socket, &QWebSocket::binaryMessageReceived, this,
            [this](const QByteArray &data) { onMessage(QString::fromUtf8(data)); });
    const auto onError = [this, socket, connection](QAbstractSocket::SocketError) {
        if (m_socket == socket)
            m_lastError = QStringLiteral("%1: %2").arg(m_server, socket->errorString());
        QTimer::singleShot(0, this, [this, socket, connection] {
            if (connection == m_connection && m_socket == socket && socket->state() == QAbstractSocket::UnconnectedState)
                onClosed(socket);
        });
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QWebSocket::errorOccurred, this, onError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, onError);
#endif
    m_connectTimer->start(kConnectTimeoutMs);
    socket->open(url);
}

void VeadotubeClient::discardSocket()
{
    m_connectTimer->stop();
    m_opened = false;
    m_pttSent = -1;
    if (!m_socket)
        return;
    QWebSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        // Let the last messages (releasing push-to-talk) and the close frame go
        // out: deleting a connected socket drops what it hasn't written yet.
        // The socket outlives this client for a moment if necessary.
        socket->setParent(nullptr);
        connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(2000, socket, &QObject::deleteLater);
        socket->flush();
        socket->close();
    } else {
        socket->abort();
        socket->deleteLater();
    }
}

void VeadotubeClient::onOpened(QWebSocket *socket)
{
    if (socket != m_socket)
        return;
    m_opened = true;
    m_connectTimer->stop();
    m_pollTimer->stop();
    if (m_candidate >= 0 && m_candidate < m_candidates.size())
        m_instanceName = m_candidates.at(m_candidate).name;
    setStatus(Status::Connected);
    refreshStates();
    if (m_pushToTalk)
        sendPushToTalk(m_talking);
}

void VeadotubeClient::onClosed(QWebSocket *socket)
{
    if (socket != m_socket)
        return;
    const bool opened = m_opened;
    if (m_lastError.isEmpty())
        m_lastError = QStringLiteral("%1: %2").arg(m_server, socket->errorString());
    discardSocket();
    if (!m_enabled)
        return;
    if (!opened) {
        tryNext(); // a left-over file, or mini's server is off: try the next one
        return;
    }
    m_instanceName.clear();
    setStatus(Status::NotRunning, QStringLiteral("veadotube closed the connection"));
    m_pollTimer->start();
}

void VeadotubeClient::onMessage(const QString &message)
{
    QVariantList states;
    if (!parseStateList(message, states))
        return;
    if (states == m_states)
        return;
    m_states = states;
    emit statesChanged();
}

void VeadotubeClient::send(const QString &message)
{
    if (m_socket && m_opened)
        m_socket->sendTextMessage(message);
}

void VeadotubeClient::setStatus(Status status, const QString &detail)
{
    if (status == m_status && detail == m_detail)
        return;
    m_status = status;
    m_detail = detail;
    emit statusChanged();
}

// --- Actions -------------------------------------------------------------------------

void VeadotubeClient::setPushToTalk(bool enabled)
{
    if (enabled == m_pushToTalk)
        return;
    if (!enabled && m_pttSent == 1)
        sendPushToTalk(false); // don't leave mini's mouth held open
    m_pushToTalk = enabled;
    if (enabled && m_talking)
        sendPushToTalk(true);
}

void VeadotubeClient::setTalking(bool talking)
{
    m_talking = talking;
    if (m_pushToTalk)
        sendPushToTalk(talking);
}

void VeadotubeClient::sendPushToTalk(bool on)
{
    if (!m_socket || !m_opened || m_pttSent == int(on))
        return;
    send(pushToTalkMessage(on));
    m_pttSent = int(on);
}

void VeadotubeClient::refreshStates()
{
    send(listStatesMessage());
}

void VeadotubeClient::setState(const QString &stateId)
{
    if (stateId.trimmed().isEmpty())
        return;
    send(setStateMessage(stateId.trimmed()));
}

QString VeadotubeClient::listStatesMessage()
{
    return QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list"}})");
}

QString VeadotubeClient::setStateMessage(const QString &stateId)
{
    return QStringLiteral(R"(nodes:{"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"set","state":)")
        + jsonString(stateId) + QStringLiteral("}}");
}

QString VeadotubeClient::pushToTalkMessage(bool on)
{
    return on ? QStringLiteral(R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":true}})")
              : QStringLiteral(R"(nodes:{"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":false}})");
}

QVariantList VeadotubeClient::parseStates(const QString &message)
{
    QVariantList out;
    parseStateList(message, out);
    return out;
}
