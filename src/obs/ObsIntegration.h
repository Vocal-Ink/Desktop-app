#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class ObsWebSocketClient;

// Connects to OBS Studio (obs-websocket v5, bundled with OBS 28+) and mirrors
// what the user says into the stream:
//  * subtitles: sets the text of a chosen Text (GDI+/FreeType 2) source, then clears it
//  * captions:  sends native closed captions (CEA-608) while streaming
//  * indicator: shows a chosen source (e.g. a "talking" avatar) while the voice plays
class ObsIntegration : public QObject
{
    Q_OBJECT
public:
    enum class Status { Disabled, Connecting, Connected, AuthFailed, Error };
    Q_ENUM(Status)

    struct Config
    {
        bool enabled = false;
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 4455;
        QString password;
        bool subtitles = false;
        QString subtitleSource;
        int clearAfterMs = 4000; // 0 = keep the last line
        bool captions = false;
        bool indicator = false;
        QString indicatorSource;
    };

    explicit ObsIntegration(QObject *parent = nullptr);
    ~ObsIntegration() override;

    void setConfig(const Config &config); // (re)connects when connection settings change
    Config config() const { return m_config; }
    Status status() const { return m_status; }
    QString statusText() const { return m_statusText; }

    // Speech events (wired to SpeechQueue).
    void utteranceStarted(const QString &text);
    void utteranceFinished(const QString &text);

    // Helpers for the settings UI. Callbacks run on the main thread; on failure
    // `error` is non-empty.
    using ListCallback = std::function<void(const QStringList &names, const QString &error)>;
    using DoneCallback = std::function<void(bool ok, const QString &message)>;
    void listTextSources(ListCallback callback);
    void listAllSources(ListCallback callback);
    void createTextSource(const QString &name, DoneCallback callback);          // in the current program scene
    void addBrowserOverlay(const QString &name, const QUrl &url, DoneCallback callback);
    void testSubtitle(DoneCallback callback);
    void reconnect();

    ObsWebSocketClient *client() const { return m_client; }

signals:
    void statusChanged(ObsIntegration::Status status, const QString &text);

private:
    void setStatus(Status status, const QString &text);
    void setSubtitleText(const QString &text);
    void setIndicator(bool visible);
    void startConnecting();
    void sendSubtitle(const QString &source, const QString &text, DoneCallback done = {});
    void finishLater(const DoneCallback &callback, bool ok, const QString &message);
    QString connectedText() const;
    void reportProblem(const QString &problem); // shown in the status text while connected
    void clearProblem();

    Config m_config;
    ObsWebSocketClient *m_client = nullptr;
    Status m_status = Status::Disabled;
    QString m_statusText;
    class QTimer *m_clearTimer = nullptr;
    QString m_indicatorScene;
    int m_indicatorItemId = -1;
    quint64 m_indicatorRequest = 0; // bumped on every show/hide so stale lookups are dropped
    bool m_subtitleShown = false;   // OBS shows subtitle text we set and haven't cleared yet
};
