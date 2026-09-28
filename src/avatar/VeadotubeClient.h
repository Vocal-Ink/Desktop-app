#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QWebSocket;
class QTimer;

// veadotube mini over its WebSocket API. mini has no mouth input: its mouth
// follows a microphone ("Vocal Ink Mic"). Vocal Ink switches mini's
// push-to-talk on while it speaks (so the mouth moves for the voice only) and
// can switch avatar states (talking / idle / mic live).
// Instances announce themselves as JSON files in ~/.veadotube/instances
// ({"time", "name", "id", "server": "127.0.0.1:2424"}); stale ones are skipped.
// Connect to ws://<server>?n=Vocal%20Ink; messages are "nodes: {json}", e.g.
//   {"event":"payload","type":"boolean","id":"mini","payload":{"event":"set","value":true}}
//   {"event":"payload","type":"stateEvents","id":"mini","payload":{"event":"list"}}
// The WebSocket server must be on in mini's program settings, and "use
// websocket" in its push-to-talk section for the push-to-talk part.
//
// Discovery: the newest instance file (by its "time") is tried first; if it
// doesn't answer, the next newest, and so on. While enabled and not connected
// the folder is checked again every few seconds.
class VeadotubeClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY statusChanged) // technical error text, may be empty
    Q_PROPERTY(QString instanceName READ instanceName NOTIFY statusChanged)
    Q_PROPERTY(QVariantList states READ states NOTIFY statesChanged) // [{id, name}]
public:
    enum class Status { Off, Searching, NotRunning, Connected, Error };
    Q_ENUM(Status)

    explicit VeadotubeClient(QObject *parent = nullptr);
    ~VeadotubeClient() override;

    void setEnabled(bool enabled);
    void setPushToTalk(bool enabled); // drive mini's push-to-talk from speech start/stop
    void setTalking(bool talking);    // push-to-talk on/off (when enabled)
    void setInstancesDirForTesting(const QString &dir);

    Status status() const { return m_status; }
    QString detail() const { return m_detail; }
    QString instanceName() const { return m_instanceName; }
    QVariantList states() const { return m_states; }

    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void refreshStates();
    Q_INVOKABLE void setState(const QString &stateId);

    // --- Additions -----------------------------------------------------------
    struct Instance
    {
        QString file;
        QString id;     // "mini-…"
        QString name;   // "veadotube mini"
        QString server; // "127.0.0.1:2424"
        qint64 time = 0; // unix seconds, refreshed while it runs
    };
    // Instance files in `dir`, newest first (files without a server are skipped).
    static QList<Instance> readInstances(const QString &dir);
    static QString defaultInstancesDir(); // ~/.veadotube/instances
    QString instancesDir() const;
    QString server() const { return m_server; } // connected (or being tried), "host:port"
    bool isEnabled() const { return m_enabled; }
    bool pushToTalk() const { return m_pushToTalk; }
    void setPollIntervalForTesting(int ms);

    // The text frames sent to mini (public for tests).
    static QString listStatesMessage();
    static QString setStateMessage(const QString &stateId);
    static QString pushToTalkMessage(bool on);
    // States from a "list" answer (lenient about the exact shape).
    static QVariantList parseStates(const QString &message);

signals:
    void statusChanged();
    void statesChanged();

private:
    void scan();
    void tryNext();
    void openSocket(const Instance &instance);
    void discardSocket();
    void onOpened(QWebSocket *socket);
    void onClosed(QWebSocket *socket);
    void onMessage(const QString &message);
    void send(const QString &message);
    void sendPushToTalk(bool on);
    void setStatus(Status status, const QString &detail = QString());

    Status m_status = Status::Off;
    QString m_detail;
    QString m_instanceName;
    QVariantList m_states;

    bool m_enabled = false;
    bool m_pushToTalk = true;
    bool m_talking = false;
    int m_pttSent = -1; // last push-to-talk value sent: -1 unknown, 0, 1
    QString m_dir;      // "" = defaultInstancesDir()
    QList<Instance> m_candidates;
    int m_candidate = -1;
    QString m_server;
    QString m_lastError;
    QPointer<QWebSocket> m_socket;
    quint64 m_connection = 0;
    bool m_opened = false;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_connectTimer = nullptr;
};
