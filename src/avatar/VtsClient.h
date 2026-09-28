#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <array>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <vector>

class SecretStore;
class QWebSocket;
class QTimer;
class QUdpSocket;

// VTube Studio plugin API client (ws://localhost:8001 by default; the protocol
// is documented at github.com/DenchiSoft/VTubeStudio).
// Flow: connect -> AuthenticationTokenRequest (VTS shows "Allow Vocal Ink?")
// -> token saved in SecretStore ("vtubestudio") -> AuthenticationRequest on
// every connection. Then: InjectParameterDataRequest for the mouth (re-sent at
// least once a second while in control, or VTS drops our values), hotkeys and
// expressions of the current model (refreshed on ModelLoadedEvent).
// Listens for VTS's UDP broadcast (port 47779) to learn when VTS is running,
// its API port, and whether "Allow Plugin API access" is off. Reconnects with
// backoff while enabled.
//
// Port: the one from the settings is always tried first. When nothing answers
// there but VTS's broadcast names another port, that one is used (and shown
// by port()) until the settings port changes.
class VtsClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY statusChanged) // technical error text, may be empty
    Q_PROPERTY(int port READ port NOTIFY statusChanged)          // in use (from settings or VTS's broadcast)
    Q_PROPERTY(bool connected READ isConnected NOTIFY statusChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(QVariantList hotkeys READ hotkeys NOTIFY modelChanged)         // [{id, name, type}]
    Q_PROPERTY(QVariantList expressions READ expressions NOTIFY modelChanged) // [{file, name, active}]
    Q_PROPERTY(QStringList parameters READ parameters NOTIFY modelChanged)    // input parameter ids
public:
    enum class Status {
        Off,             // disabled in settings
        Searching,       // waiting for the secret store / VTS's broadcast
        Connecting,
        NotRunning,      // nothing listening on the port and no broadcast seen
        ApiOff,          // VTS is running but "Allow Plugin API access" is off
        WaitingForAllow, // the "Allow" popup is showing in VTube Studio
        Denied,          // the user clicked Deny (or revoked the token)
        Connected,
        Error
    };
    Q_ENUM(Status)

    explicit VtsClient(SecretStore *secrets, QObject *parent = nullptr);
    ~VtsClient() override;

    void setEnabled(bool enabled);
    void setPort(quint16 port);
    void setUrlForTesting(const QUrl &url);
    // Which input parameters the mouth drives ("" = don't drive that one).
    void setMouthParameters(const QString &openParam, const QString &formParam);
    // Tell VTS the face is found while we drive the mouth (no "tracking lost"
    // animation for people who don't use face tracking).
    void setFaceFound(bool faceFound);
    // Also create and drive "VocalInkVolume" (0..1) and "VocalInkSpeaking" (0/1)
    // custom parameters that users can map to anything in their model.
    void setCustomParameters(bool enabled);

    Status status() const { return m_status; }
    QString detail() const { return m_detail; }
    int port() const { return m_port; }
    bool isConnected() const { return m_status == Status::Connected; }
    QString modelName() const { return m_modelName; }
    QVariantList hotkeys() const { return m_hotkeys; }
    QVariantList expressions() const { return m_expressions; }
    QStringList parameters() const { return m_parameters; }

    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void requestAccess(); // ask for a new token (after Denied)
    Q_INVOKABLE void forgetAccess();  // drop the saved token
    Q_INVOKABLE void refreshModelData();
    Q_INVOKABLE void triggerHotkey(const QString &hotkeyId);
    Q_INVOKABLE void setExpression(const QString &expressionFile, bool active);

    // Mouth open 0..1 and form -1..1 (smile/frown), plus the five vowel weights
    // (A I U E O, 0..1) for models rigged to VoiceA..VoiceO. Call at up to 30 Hz
    // while talking; values that didn't change are not re-sent more than once a
    // second. After release() nothing stays injected.
    void setMouth(float open, float form, const std::array<float, 5> &vowels);
    void release();

    // Wait for SecretStore::loaded before authenticating (otherwise VTS would
    // ask "Allow?" again on every launch); the token is stored as
    // Secrets::VTubeStudio.

    // --- Additions -----------------------------------------------------------
    // Shown in VTS's "Allow?" popup: a PNG of exactly 128x128 (anything else is
    // ignored, VTS would reject the request).
    void setPluginIcon(const QByteArray &png128);
    // Drives "VocalInkSpeaking" (with custom parameters on).
    void setTalking(bool talking);
    bool isEnabled() const { return m_enabled; }
    bool isInControl() const { return m_inControl; } // injecting (between setMouth() and release())
    // VTS "form" (-1..1) as the value injected into its MouthSmile-style
    // parameter (0..1, 0.5 = neutral).
    static float formToParameter(float form) { return (form + 1.0f) * 0.5f; }

    // For tests: reconnect delays (default 1 s doubling to 30 s) and the UDP
    // port VTS broadcasts on (47779; 0 = don't listen). setUrlForTesting()
    // turns the broadcast listener off.
    void setReconnectDelays(int firstMs, int maxMs);
    void setDiscoveryPortForTesting(quint16 port);
    static constexpr quint16 kDiscoveryPort = 47779;
    static constexpr int kKeepAliveMs = 800;

signals:
    void statusChanged();
    void modelChanged();
    // VTube Studio refused access (Deny clicked, or the saved token was revoked).
    void accessDenied();

private:
    struct Param
    {
        QByteArray prefix; // {"id":"MouthOpen","value":
        int source;        // what drives it (see VtsClient.cpp)
    };

    void start();
    void openSocket();
    void discardSocket();
    void onSocketOpened(QWebSocket *socket);
    void onSocketClosed(QWebSocket *socket);
    void onTextMessage(const QString &message);
    void onApiError(const QString &requestType, const QJsonObject &data);
    void onAuthenticated();
    void authenticate();
    void requestToken();
    void deny(const QString &detail, bool fromVts);
    void scheduleReconnect();
    void setStatus(Status status, const QString &detail = QString());
    void setDetail(const QString &detail);
    QString sendRequest(const QString &messageType, const QJsonObject &data = QJsonObject());
    void createCustomParameters();
    void readModel(const QString &messageType, const QJsonObject &data);

    void startDiscovery();
    void stopDiscovery();
    void onDiscoveryReadyRead();
    bool broadcastFresh() const;

    void rebuildInjectTemplate();
    void currentValues(std::vector<float> &out) const;
    void sendInject(bool force);
    QString token() const;
    void storeToken(const QString &token);
    bool secretsReady() const;
    quint16 effectivePort() const;

    SecretStore *m_secrets;
    Status m_status = Status::Off;
    QString m_detail;
    int m_port = 8001;
    QString m_modelName;
    QVariantList m_hotkeys;
    QVariantList m_expressions;
    QStringList m_parameters;

    // Connection
    bool m_enabled = false;
    quint16 m_settingsPort = 8001;
    bool m_useBroadcastPort = false;
    QUrl m_testUrl;
    QPointer<QWebSocket> m_socket;
    quint64 m_connection = 0;
    bool m_socketOpened = false;
    QString m_socketError;
    bool m_authenticated = false;
    bool m_denied = false;            // don't ask for a token until requestAccess()
    bool m_tokenRequested = false;    // waiting for the "Allow?" answer on this connection
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_connectTimer = nullptr; // gives up on a connection that never opens
    QTimer *m_tokenRetryTimer = nullptr;
    int m_reconnectMinMs = 1000;
    int m_reconnectMaxMs = 30000;
    int m_reconnectDelayMs = 1000;
    quint64 m_nextRequest = 0;
    QHash<QString, QString> m_pending; // requestID -> request messageType
    QString m_memoryToken;             // without a SecretStore
    QString m_iconBase64;

    // Discovery
    quint16 m_discoveryPort = kDiscoveryPort;
    QUdpSocket *m_udp = nullptr;
    QElapsedTimer m_lastBroadcast;
    bool m_broadcastActive = false;
    quint16 m_broadcastPort = 0;

    // Mouth injection
    QString m_openParam = QStringLiteral("MouthOpen");
    QString m_formParam = QStringLiteral("MouthSmile");
    bool m_faceFound = false;
    bool m_customParams = false;
    int m_customCreated = 0; // which custom parameters VTS confirmed (bits)
    bool m_talking = false;
    bool m_inControl = false;
    float m_open = 0.0f;
    float m_form = 0.0f;
    std::array<float, 5> m_vowels{};
    std::vector<Param> m_params;
    QByteArray m_injectHead;
    std::vector<float> m_values;
    std::vector<float> m_lastSent;
    QTimer *m_keepAliveTimer = nullptr;
    bool m_injectError = false; // detail currently shows an injection error
};
