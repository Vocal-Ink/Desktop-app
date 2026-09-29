#include "avatar/VtsClient.h"

#include "core/SecretStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace {

const QString kApiName = QStringLiteral("VTubeStudioPublicAPI");
const QString kApiVersion = QStringLiteral("1.0");
const QString kPluginName = QStringLiteral("Vocal Ink");
const QString kPluginDeveloper = QStringLiteral("Vocal Ink");
const QString kInjectId = QStringLiteral("inject"); // every injection uses this request id
const QString kVolumeParam = QStringLiteral("VocalInkVolume");
const QString kSpeakingParam = QStringLiteral("VocalInkSpeaking");
const QStringList kVowelParams = {QStringLiteral("VoiceA"), QStringLiteral("VoiceI"), QStringLiteral("VoiceU"),
                                  QStringLiteral("VoiceE"), QStringLiteral("VoiceO")};

constexpr int kConnectTimeoutMs = 8000;
constexpr int kTokenRetryMs = 3000;
constexpr int kBroadcastFreshMs = 5000; // VTS broadcasts every 2 s
constexpr float kChangeThreshold = 0.01f;

// VTS error ids (see the API's ErrorID.cs).
constexpr int kErrApiAccessDeactivated = 1;
constexpr int kErrRequiresAuthentication = 8;
constexpr int kErrTokenRequestDenied = 50;
constexpr int kErrTokenRequestOngoing = 51;

enum Source { SrcOpen, SrcForm, SrcVowel0, SrcVolume = SrcVowel0 + 5, SrcSpeaking };
enum CustomBits { CustomVolume = 1, CustomSpeaking = 2 };

QByteArray jsonString(const QString &s)
{
    // Serialise through a one-element array to get correct escaping.
    QByteArray a = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    return a.mid(1, a.size() - 2);
}

float clamp01(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

bool isPng128(const QByteArray &png)
{
    static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
    if (png.size() < 24 || !png.startsWith(signature) || png.mid(12, 4) != "IHDR")
        return false;
    return qFromBigEndian<quint32>(png.constData() + 16) == 128 && qFromBigEndian<quint32>(png.constData() + 20) == 128;
}

} // namespace

VtsClient::VtsClient(SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_reconnectTimer(new QTimer(this))
    , m_connectTimer(new QTimer(this))
    , m_tokenRetryTimer(new QTimer(this))
    , m_keepAliveTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_enabled)
            start();
    });

    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, [this] {
        QWebSocket *socket = m_socket;
        if (!socket || m_socketOpened)
            return;
        m_socketError = QStringLiteral("Timed out");
        socket->abort();
        if (m_socket == socket)
            onSocketClosed(socket);
    });

    m_tokenRetryTimer->setSingleShot(true);
    connect(m_tokenRetryTimer, &QTimer::timeout, this, [this] {
        if (m_socketOpened && !m_authenticated && !m_denied && !m_tokenRequested && token().isEmpty())
            requestToken();
    });

    // Re-sent before VTS's one-second timeout while we hold the mouth.
    m_keepAliveTimer->setSingleShot(true);
    m_keepAliveTimer->setInterval(kKeepAliveMs);
    connect(m_keepAliveTimer, &QTimer::timeout, this, [this] {
        if (m_inControl)
            sendInject(true);
    });

    if (m_secrets) {
        connect(m_secrets, &SecretStore::loaded, this, [this] {
            if (m_enabled && !m_socket && m_status == Status::Searching && !m_reconnectTimer->isActive())
                start();
        });
    }
    rebuildInjectTemplate();
}

VtsClient::~VtsClient()
{
    discardSocket();
}

// --- Configuration ------------------------------------------------------------------

void VtsClient::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (!enabled) {
        m_inControl = false;
        discardSocket();
        m_reconnectTimer->stop();
        stopDiscovery();
        m_reconnectDelayMs = m_reconnectMinMs;
        setStatus(Status::Off);
        return;
    }
    m_denied = false; // switching it on again is a fresh start
    m_reconnectDelayMs = m_reconnectMinMs;
    start();
}

void VtsClient::setPort(quint16 port)
{
    if (port == 0 || port == m_settingsPort)
        return;
    m_settingsPort = port;
    m_useBroadcastPort = false;
    if (m_enabled && m_testUrl.isEmpty() && m_status != Status::Denied)
        reconnect();
    else if (m_port != effectivePort()) {
        m_port = effectivePort();
        emit statusChanged();
    }
}

void VtsClient::setUrlForTesting(const QUrl &url)
{
    m_testUrl = url;
    m_discoveryPort = 0;
    stopDiscovery();
    if (m_enabled)
        reconnect();
}

void VtsClient::setReconnectDelays(int firstMs, int maxMs)
{
    m_reconnectMinMs = std::max(10, firstMs);
    m_reconnectMaxMs = std::max(m_reconnectMinMs, maxMs);
    m_reconnectDelayMs = m_reconnectMinMs;
}

void VtsClient::setDiscoveryPortForTesting(quint16 port)
{
    stopDiscovery();
    m_discoveryPort = port;
    if (m_enabled)
        startDiscovery();
}

void VtsClient::setMouthParameters(const QString &openParam, const QString &formParam)
{
    const QString open = openParam.trimmed();
    const QString form = formParam.trimmed();
    if (open == m_openParam && form == m_formParam)
        return;
    m_openParam = open;
    m_formParam = form;
    rebuildInjectTemplate();
}

void VtsClient::setFaceFound(bool faceFound)
{
    if (faceFound == m_faceFound)
        return;
    m_faceFound = faceFound;
    rebuildInjectTemplate();
}

void VtsClient::setCustomParameters(bool enabled)
{
    if (enabled == m_customParams)
        return;
    m_customParams = enabled;
    m_customCreated = 0;
    if (enabled && m_authenticated)
        createCustomParameters();
    rebuildInjectTemplate();
}

void VtsClient::setPluginIcon(const QByteArray &png128)
{
    m_iconBase64 = isPng128(png128) ? QString::fromLatin1(png128.toBase64()) : QString();
}

void VtsClient::setTalking(bool talking)
{
    if (talking == m_talking)
        return;
    m_talking = talking;
    if (m_inControl)
        sendInject(false);
}

// --- Connection ----------------------------------------------------------------------

QString VtsClient::token() const
{
    return m_secrets ? m_secrets->get(Secrets::VTubeStudio) : m_memoryToken;
}

void VtsClient::storeToken(const QString &token)
{
    if (m_secrets)
        m_secrets->set(Secrets::VTubeStudio, token);
    else
        m_memoryToken = token;
}

bool VtsClient::secretsReady() const
{
    return !m_secrets || m_secrets->isLoaded();
}

quint16 VtsClient::effectivePort() const
{
    return m_useBroadcastPort && m_broadcastPort ? m_broadcastPort : m_settingsPort;
}

void VtsClient::start()
{
    if (!m_enabled)
        return;
    startDiscovery();
    if (!secretsReady()) {
        setStatus(Status::Searching);
        return;
    }
    openSocket();
}

void VtsClient::reconnect()
{
    if (!m_enabled)
        return;
    m_reconnectTimer->stop();
    m_reconnectDelayMs = m_reconnectMinMs;
    discardSocket();
    if (secretsReady())
        setStatus(Status::Connecting);
    start();
}

void VtsClient::requestAccess()
{
    m_denied = false;
    if (!m_enabled || m_authenticated)
        return;
    if (m_socketOpened) {
        if (!m_tokenRequested && token().isEmpty())
            requestToken();
        return;
    }
    reconnect();
}

void VtsClient::forgetAccess()
{
    storeToken(QString());
    if (!m_enabled)
        return;
    // Don't pop up "Allow?" again right away: requestAccess() asks.
    m_denied = true;
    m_inControl = false;
    discardSocket();
    m_reconnectTimer->stop();
    setStatus(Status::Denied);
}

void VtsClient::openSocket()
{
    discardSocket();
    ++m_connection;
    m_socketOpened = false;
    m_socketError.clear();
    const quint16 port = effectivePort();
    if (m_port != port) {
        m_port = port;
        emit statusChanged();
    }
    // Retries keep showing why the last attempt failed instead of flickering.
    if (m_status != Status::NotRunning && m_status != Status::ApiOff)
        setStatus(Status::Connecting);

    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    socket->setProxy(QNetworkProxy::NoProxy); // VTube Studio is local
    m_socket = socket;
    const quint64 connection = m_connection;
    connect(socket, &QWebSocket::connected, this, [this, socket] { onSocketOpened(socket); });
    connect(socket, &QWebSocket::disconnected, this, [this, socket] { onSocketClosed(socket); });
    connect(socket, &QWebSocket::textMessageReceived, this, &VtsClient::onTextMessage);
    const auto onError = [this, socket, connection](QAbstractSocket::SocketError) {
        if (m_socket == socket)
            m_socketError = socket->errorString();
        // A refused connection does not always end with disconnected().
        QTimer::singleShot(0, this, [this, socket, connection] {
            if (connection == m_connection && m_socket == socket && socket->state() == QAbstractSocket::UnconnectedState)
                onSocketClosed(socket);
        });
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QWebSocket::errorOccurred, this, onError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, onError);
#endif

    QUrl url = m_testUrl;
    if (url.isEmpty()) {
        url.setScheme(QStringLiteral("ws"));
        url.setHost(QStringLiteral("127.0.0.1"));
        url.setPort(port);
    }
    m_connectTimer->start(kConnectTimeoutMs);
    socket->open(url);
}

void VtsClient::discardSocket()
{
    m_connectTimer->stop();
    m_tokenRetryTimer->stop();
    m_keepAliveTimer->stop();
    const bool hadCustom = m_customCreated != 0;
    m_authenticated = false;
    m_tokenRequested = false;
    m_socketOpened = false;
    m_pending.clear();
    m_customCreated = 0;
    m_lastSent.clear();
    if (hadCustom)
        rebuildInjectTemplate();
    if (!m_socket)
        return;
    QWebSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        // Let the last requests (closing the mouth) and the close frame go out:
        // deleting a connected socket drops what it hasn't written yet. The
        // socket outlives this client for a moment if necessary.
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

void VtsClient::onSocketOpened(QWebSocket *socket)
{
    if (socket != m_socket)
        return;
    m_socketOpened = true;
    m_connectTimer->stop();
    setStatus(Status::Connecting);
    authenticate();
}

void VtsClient::onSocketClosed(QWebSocket *socket)
{
    if (socket != m_socket)
        return;
    const bool opened = m_socketOpened;
    const bool wasAuthenticated = m_authenticated;
    const QString error = m_socketError.isEmpty() ? socket->errorString() : m_socketError;
    discardSocket();
    if (!m_enabled || m_denied)
        return;

    if (!opened) {
        // Nothing answered on this port. VTS's broadcast tells why, and may
        // name another port.
        if (broadcastFresh() && m_broadcastActive && m_broadcastPort && m_broadcastPort != m_settingsPort)
            m_useBroadcastPort = !m_useBroadcastPort;
        if (broadcastFresh() && !m_broadcastActive)
            setStatus(Status::ApiOff, error);
        else
            setStatus(Status::NotRunning, error);
    } else {
        if (wasAuthenticated)
            m_reconnectDelayMs = m_reconnectMinMs;
        setStatus(Status::NotRunning, QStringLiteral("VTube Studio closed the connection"));
    }
    scheduleReconnect();
}

void VtsClient::scheduleReconnect()
{
    if (!m_enabled || m_denied)
        return;
    m_reconnectTimer->start(m_reconnectDelayMs);
    m_reconnectDelayMs = std::min(m_reconnectMaxMs, m_reconnectDelayMs * 2);
}

void VtsClient::setStatus(Status status, const QString &detail)
{
    if (status == m_status && detail == m_detail)
        return;
    m_status = status;
    m_detail = detail;
    m_injectError = false;
    emit statusChanged();
}

void VtsClient::setDetail(const QString &detail)
{
    if (detail == m_detail)
        return;
    m_detail = detail;
    emit statusChanged();
}

QString VtsClient::sendRequest(const QString &messageType, const QJsonObject &data)
{
    if (!m_socket || !m_socketOpened)
        return QString();
    const QString id = QStringLiteral("vi%1").arg(++m_nextRequest);
    m_pending.insert(id, messageType);
    QJsonObject message{{QStringLiteral("apiName"), kApiName},
                        {QStringLiteral("apiVersion"), kApiVersion},
                        {QStringLiteral("requestID"), id},
                        {QStringLiteral("messageType"), messageType}};
    if (!data.isEmpty())
        message.insert(QStringLiteral("data"), data);
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    return id;
}

// --- Authentication -----------------------------------------------------------------

void VtsClient::authenticate()
{
    const QString t = token();
    if (!t.isEmpty()) {
        sendRequest(QStringLiteral("AuthenticationRequest"),
                    {{QStringLiteral("pluginName"), kPluginName},
                     {QStringLiteral("pluginDeveloper"), kPluginDeveloper},
                     {QStringLiteral("authenticationToken"), t}});
        return;
    }
    if (m_denied) {
        deny(QString(), false);
        return;
    }
    requestToken();
}

void VtsClient::requestToken()
{
    QJsonObject data{{QStringLiteral("pluginName"), kPluginName}, {QStringLiteral("pluginDeveloper"), kPluginDeveloper}};
    if (!m_iconBase64.isEmpty())
        data.insert(QStringLiteral("pluginIcon"), m_iconBase64);
    // No timeout: VTS answers once the user clicks Allow or Deny.
    sendRequest(QStringLiteral("AuthenticationTokenRequest"), data);
    m_tokenRequested = true;
    setStatus(Status::WaitingForAllow);
}

void VtsClient::deny(const QString &detail, bool fromVts)
{
    m_denied = true;
    m_inControl = false;
    discardSocket();
    m_reconnectTimer->stop();
    setStatus(Status::Denied, detail);
    if (fromVts)
        emit accessDenied();
}

void VtsClient::onAuthenticated()
{
    m_authenticated = true;
    m_denied = false;
    m_tokenRequested = false;
    m_reconnectDelayMs = m_reconnectMinMs;
    setStatus(Status::Connected);
    sendRequest(QStringLiteral("EventSubscriptionRequest"),
                {{QStringLiteral("eventName"), QStringLiteral("ModelLoadedEvent")},
                 {QStringLiteral("subscribe"), true},
                 {QStringLiteral("config"), QJsonObject()}});
    refreshModelData();
    if (m_customParams)
        createCustomParameters();
    if (m_inControl)
        sendInject(true);
}

// --- Messages ------------------------------------------------------------------------

void VtsClient::onTextMessage(const QString &message)
{
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    if (obj.isEmpty())
        return;
    const QString type = obj.value(QStringLiteral("messageType")).toString();
    const QString id = obj.value(QStringLiteral("requestID")).toString();
    const QJsonObject data = obj.value(QStringLiteral("data")).toObject();

    if (id == kInjectId) {
        if (type == QLatin1String("APIError")) {
            // Typically 453 (no such parameter) or 454 (another plugin drives it).
            const QString text = QStringLiteral("%1 (errorID %2)")
                                     .arg(data.value(QStringLiteral("message")).toString())
                                     .arg(data.value(QStringLiteral("errorID")).toInt());
            setDetail(text);
            m_injectError = true;
        } else if (m_injectError) {
            m_injectError = false;
            setDetail(QString());
        }
        return;
    }

    const QString request = m_pending.take(id);
    if (type == QLatin1String("APIError")) {
        onApiError(request, data);
        return;
    }
    if (type == QLatin1String("AuthenticationTokenResponse")) {
        m_tokenRequested = false;
        const QString t = data.value(QStringLiteral("authenticationToken")).toString();
        if (t.isEmpty()) {
            setStatus(Status::Error, QStringLiteral("VTube Studio sent an empty token"));
            return;
        }
        storeToken(t);
        setStatus(Status::Connecting);
        authenticate();
        return;
    }
    if (type == QLatin1String("AuthenticationResponse")) {
        if (data.value(QStringLiteral("authenticated")).toBool()) {
            onAuthenticated();
        } else {
            // The token was revoked (or belongs to another VTS install).
            storeToken(QString());
            deny(data.value(QStringLiteral("reason")).toString(), true);
        }
        return;
    }
    if (type == QLatin1String("ModelLoadedEvent")) {
        const bool loaded = data.value(QStringLiteral("modelLoaded")).toBool();
        const QString name = loaded ? data.value(QStringLiteral("modelName")).toString() : QString();
        if (name != m_modelName) {
            m_modelName = name;
            emit modelChanged();
        }
        refreshModelData();
        return;
    }
    if (type == QLatin1String("ParameterCreationResponse")) {
        const QString name = data.value(QStringLiteral("parameterName")).toString();
        if (name == kVolumeParam)
            m_customCreated |= CustomVolume;
        else if (name == kSpeakingParam)
            m_customCreated |= CustomSpeaking;
        rebuildInjectTemplate();
        return;
    }
    readModel(type, data);
}

void VtsClient::onApiError(const QString &requestType, const QJsonObject &data)
{
    const int code = data.value(QStringLiteral("errorID")).toInt(-1);
    const QString text = QStringLiteral("%1 (errorID %2)").arg(data.value(QStringLiteral("message")).toString()).arg(code);

    if (code == kErrApiAccessDeactivated) {
        discardSocket();
        setStatus(Status::ApiOff, text);
        scheduleReconnect();
        return;
    }
    if (requestType == QLatin1String("AuthenticationTokenRequest")) {
        m_tokenRequested = false;
        if (code == kErrTokenRequestDenied) {
            deny(text, true);
        } else if (code == kErrTokenRequestOngoing) {
            // Another "Allow?" popup is still open (e.g. from before a reconnect).
            setStatus(Status::WaitingForAllow, text);
            m_tokenRetryTimer->start(kTokenRetryMs);
        } else {
            setStatus(Status::Error, text);
        }
        return;
    }
    if (requestType == QLatin1String("AuthenticationRequest")) {
        setStatus(Status::Error, text);
        return;
    }
    if (code == kErrRequiresAuthentication) {
        m_authenticated = false;
        setStatus(Status::Connecting, text);
        authenticate();
        return;
    }
    // Hotkey, expression and parameter errors: shown, the connection stays.
    setDetail(text);
}

void VtsClient::readModel(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String("CurrentModelResponse")) {
        const bool loaded = data.value(QStringLiteral("modelLoaded")).toBool();
        const QString name = loaded ? data.value(QStringLiteral("modelName")).toString() : QString();
        if (name != m_modelName) {
            m_modelName = name;
            emit modelChanged();
        }
    } else if (type == QLatin1String("HotkeysInCurrentModelResponse")) {
        QVariantList list;
        const QJsonArray hotkeys = data.value(QStringLiteral("availableHotkeys")).toArray();
        for (const QJsonValue &v : hotkeys) {
            const QJsonObject h = v.toObject();
            const QString id = h.value(QStringLiteral("hotkeyID")).toString();
            if (id.isEmpty())
                continue;
            QString name = h.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                name = h.value(QStringLiteral("description")).toString();
            if (name.isEmpty())
                name = h.value(QStringLiteral("file")).toString();
            list.append(QVariantMap{{QStringLiteral("id"), id},
                                    {QStringLiteral("name"), name},
                                    {QStringLiteral("type"), h.value(QStringLiteral("type")).toString()},
                                    {QStringLiteral("description"), h.value(QStringLiteral("description")).toString()}});
        }
        m_hotkeys = list;
        emit modelChanged();
    } else if (type == QLatin1String("ExpressionStateResponse")) {
        QVariantList list;
        const QJsonArray expressions = data.value(QStringLiteral("expressions")).toArray();
        for (const QJsonValue &v : expressions) {
            const QJsonObject e = v.toObject();
            const QString file = e.value(QStringLiteral("file")).toString();
            if (file.isEmpty())
                continue;
            list.append(QVariantMap{{QStringLiteral("file"), file},
                                    {QStringLiteral("name"), e.value(QStringLiteral("name")).toString()},
                                    {QStringLiteral("active"), e.value(QStringLiteral("active")).toBool()}});
        }
        m_expressions = list;
        emit modelChanged();
    } else if (type == QLatin1String("InputParameterListResponse")) {
        QStringList names;
        for (const auto *key : {"defaultParameters", "customParameters"}) {
            const QJsonArray params = data.value(QLatin1String(key)).toArray();
            for (const QJsonValue &v : params) {
                const QString name = v.toObject().value(QStringLiteral("name")).toString();
                if (!name.isEmpty() && !names.contains(name))
                    names.append(name);
            }
        }
        m_parameters = names;
        emit modelChanged();
        rebuildInjectTemplate(); // the vowel parameters may have appeared
    }
}

void VtsClient::refreshModelData()
{
    if (!m_authenticated)
        return;
    sendRequest(QStringLiteral("CurrentModelRequest"));
    sendRequest(QStringLiteral("HotkeysInCurrentModelRequest"));
    sendRequest(QStringLiteral("ExpressionStateRequest"), {{QStringLiteral("details"), false}});
    sendRequest(QStringLiteral("InputParameterListRequest"));
}

void VtsClient::createCustomParameters()
{
    const auto create = [this](const QString &name, const QString &explanation) {
        sendRequest(QStringLiteral("ParameterCreationRequest"),
                    {{QStringLiteral("parameterName"), name},
                     {QStringLiteral("explanation"), explanation},
                     {QStringLiteral("min"), 0},
                     {QStringLiteral("max"), 1},
                     {QStringLiteral("defaultValue"), 0}});
    };
    create(kVolumeParam, QStringLiteral("How far Vocal Ink opens the mouth right now (0 to 1)"));
    create(kSpeakingParam, QStringLiteral("1 while Vocal Ink's voice is talking, else 0"));
}

void VtsClient::triggerHotkey(const QString &hotkeyId)
{
    if (!m_authenticated || hotkeyId.isEmpty())
        return;
    sendRequest(QStringLiteral("HotkeyTriggerRequest"), {{QStringLiteral("hotkeyID"), hotkeyId}});
}

void VtsClient::setExpression(const QString &expressionFile, bool active)
{
    if (!m_authenticated || expressionFile.isEmpty())
        return;
    sendRequest(QStringLiteral("ExpressionActivationRequest"),
                {{QStringLiteral("expressionFile"), expressionFile},
                 {QStringLiteral("fadeTime"), 0.25},
                 {QStringLiteral("active"), active}});
}

// --- Mouth ---------------------------------------------------------------------------

void VtsClient::rebuildInjectTemplate()
{
    m_params.clear();
    QStringList used;
    const auto add = [this, &used](const QString &id, int source) {
        if (id.isEmpty() || used.contains(id))
            return;
        used.append(id);
        m_params.push_back({QByteArrayLiteral("{\"id\":") + jsonString(id) + QByteArrayLiteral(",\"value\":"), source});
    };
    add(m_openParam, SrcOpen);
    add(m_formParam, SrcForm);
    for (int i = 0; i < 5; ++i) {
        // Only when this VTS has them: one unknown id fails the whole request.
        if (m_parameters.contains(kVowelParams.at(i)))
            add(kVowelParams.at(i), SrcVowel0 + i);
    }
    if (m_customParams && (m_customCreated & CustomVolume))
        add(kVolumeParam, SrcVolume);
    if (m_customParams && (m_customCreated & CustomSpeaking))
        add(kSpeakingParam, SrcSpeaking);

    m_injectHead = QByteArrayLiteral("{\"apiName\":\"VTubeStudioPublicAPI\",\"apiVersion\":\"1.0\",\"requestID\":\"")
        + kInjectId.toLatin1()
        + QByteArrayLiteral("\",\"messageType\":\"InjectParameterDataRequest\",\"data\":{\"faceFound\":")
        + (m_faceFound ? QByteArrayLiteral("true") : QByteArrayLiteral("false"))
        + QByteArrayLiteral(",\"mode\":\"set\",\"parameterValues\":[");
    m_lastSent.clear();
    if (m_inControl)
        sendInject(true);
}

void VtsClient::currentValues(std::vector<float> &out) const
{
    out.resize(m_params.size());
    for (size_t i = 0; i < m_params.size(); ++i) {
        const int source = m_params[i].source;
        float v = 0.0f;
        if (source == SrcOpen || source == SrcVolume)
            v = clamp01(m_open);
        else if (source == SrcForm)
            v = clamp01(formToParameter(std::isfinite(m_form) ? std::clamp(m_form, -1.0f, 1.0f) : 0.0f));
        else if (source == SrcSpeaking)
            v = m_talking ? 1.0f : 0.0f;
        else
            v = clamp01(m_vowels[size_t(source - SrcVowel0)]);
        out[i] = v;
    }
}

void VtsClient::sendInject(bool force)
{
    if (!m_authenticated || !m_socket || m_params.empty())
        return;
    currentValues(m_values);
    if (!force && m_lastSent.size() == m_values.size()) {
        bool changed = false;
        for (size_t i = 0; i < m_values.size() && !changed; ++i)
            changed = std::fabs(m_values[i] - m_lastSent[i]) > kChangeThreshold;
        if (!changed)
            return;
    }
    QByteArray message = m_injectHead;
    message.reserve(m_injectHead.size() + qsizetype(m_params.size()) * 48 + 4);
    for (size_t i = 0; i < m_params.size(); ++i) {
        if (i)
            message += ',';
        message += m_params[i].prefix;
        message += QByteArray::number(double(m_values[i]), 'f', 3);
        message += '}';
    }
    message += "]}}";
    m_socket->sendTextMessage(QString::fromUtf8(message));
    m_lastSent = m_values;
    m_keepAliveTimer->start();
}

void VtsClient::setMouth(float open, float form, const std::array<float, 5> &vowels)
{
    if (!m_enabled)
        return;
    m_open = open;
    m_form = form;
    m_vowels = vowels;
    m_inControl = true;
    sendInject(false);
}

void VtsClient::release()
{
    if (!m_inControl)
        return;
    m_inControl = false;
    m_open = 0.0f;
    m_form = 0.0f;
    m_vowels = {};
    // Close the mouth once if it was left open (e.g. stopped mid-word); VTS
    // hands the parameters back to tracking about a second later.
    if (m_authenticated && !m_lastSent.empty()) {
        currentValues(m_values);
        if (m_values != m_lastSent)
            sendInject(true);
    }
    m_keepAliveTimer->stop();
    m_lastSent.clear();
}

// --- Discovery -----------------------------------------------------------------------

void VtsClient::startDiscovery()
{
    if (m_udp || m_discoveryPort == 0)
        return;
    m_udp = new QUdpSocket(this);
    // Other plugins listen for the same broadcast.
    if (!m_udp->bind(QHostAddress::AnyIPv4, m_discoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        delete m_udp;
        m_udp = nullptr;
        return;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &VtsClient::onDiscoveryReadyRead);
}

void VtsClient::stopDiscovery()
{
    delete m_udp;
    m_udp = nullptr;
}

bool VtsClient::broadcastFresh() const
{
    return m_lastBroadcast.isValid() && m_lastBroadcast.elapsed() < kBroadcastFreshMs;
}

void VtsClient::onDiscoveryReadyRead()
{
    bool seen = false;
    while (m_udp && m_udp->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_udp->receiveDatagram();
        const QJsonObject obj = QJsonDocument::fromJson(datagram.data()).object();
        if (obj.value(QStringLiteral("messageType")).toString() != QLatin1String("VTubeStudioAPIStateBroadcast"))
            continue;
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        m_broadcastActive = data.value(QStringLiteral("active")).toBool();
        const int port = data.value(QStringLiteral("port")).toInt();
        m_broadcastPort = port > 0 && port < 65536 ? quint16(port) : 0;
        m_lastBroadcast.start();
        seen = true;
    }
    if (!seen || !m_enabled || m_socketOpened || m_denied)
        return;
    const bool waiting = !m_socket; // between attempts
    if (!m_broadcastActive) {
        if (waiting && m_status != Status::ApiOff)
            setStatus(Status::ApiOff, m_detail);
        return;
    }
    // VTS (or its API) just came up: try now instead of waiting for the backoff.
    if (waiting && (m_status == Status::NotRunning || m_status == Status::ApiOff) && secretsReady()) {
        if (m_broadcastPort && m_broadcastPort != m_settingsPort)
            m_useBroadcastPort = true;
        m_reconnectTimer->stop();
        m_reconnectDelayMs = m_reconnectMinMs;
        openSocket();
    }
}
