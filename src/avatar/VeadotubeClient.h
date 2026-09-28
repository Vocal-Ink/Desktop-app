#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class QWebSocket;

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

signals:
    void statusChanged();
    void statesChanged();

private:
    Status m_status = Status::Off;
    QString m_detail;
    QString m_instanceName;
    QVariantList m_states;
};
