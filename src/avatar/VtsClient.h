#pragma once

#include <QObject>
#include <array>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class SecretStore;
class QWebSocket;
class QTimer;

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

signals:
    void statusChanged();
    void modelChanged();

private:
    SecretStore *m_secrets;
    Status m_status = Status::Off;
    QString m_detail;
    int m_port = 8001;
    QString m_modelName;
    QVariantList m_hotkeys;
    QVariantList m_expressions;
    QStringList m_parameters;
};
