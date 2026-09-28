#pragma once

#include "obs/OverlayStyle.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QTcpServer;
class QTcpSocket;
class QWebSocket;
class QWebSocketServer;

// Tiny local web server for the OBS "Browser Source" overlays
// (docs/OBS.md, docs/overlay-style.md).
//   GET /            overlay page (?profile=<id>; URL options can override the style)
//   GET /overlay.js, /overlay.css, /fonts/<file>, /assets/<id>
//   GET /state       current caption as JSON (debugging)
//   WS  /ws          live events pushed to the page (?profile=<id>)
// Binds to 127.0.0.1 unless LAN access is allowed (OBS on another PC). In LAN
// mode only IP literals, localhost, this machine's own names and the allowed
// host list are accepted as Host (blocks DNS rebinding).
class OverlayServer : public QObject
{
    Q_OBJECT
public:
    explicit OverlayServer(QObject *parent = nullptr);
    ~OverlayServer() override;

    bool start(quint16 port, bool allowLan = false);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString errorString() const { return m_error; }
    QUrl overlayUrl(const QString &query = QString()) const;
    int clientCount() const { return int(m_clients.size()); }

    // --- Configuration (no restart needed) ------------------------------------
    // Profiles with their styles already resolved and validated
    // (OverlayStyle::effective). Changing them pushes {"type":"config"} to the
    // pages of that profile right away; config is replayed on connect.
    void setProfiles(const QList<OverlayProfile> &resolved);
    QList<OverlayProfile> profiles() const { return m_profiles; }
    void setStyle(const QString &profileId, const QJsonObject &resolvedStyle);
    QUrl profileUrl(const QString &profileId) const; // "main" -> "/", else "/?profile=<id>"
    // The query string old OBS links carry (Keys::OverlayLegacyQuery). Pages
    // whose URL options equal it follow their profile instead of the URL.
    void setLegacyQuery(const QString &query);
    // Images a style can reference by id (built-in PNGtuber): asset id -> absolute
    // path. Served as /assets/<id>, looked up in this map only.
    void setAssets(const QHash<QString, QString> &assetIdToPath);
    void setFontDir(const QString &dir); // default ":/fonts"; served as /fonts/<file>
    void setAllowedHosts(const QStringList &hostNames); // LAN mode extras
    // Words the page shows itself ({"speaking", "listening", "micLive", "demo": [..]}),
    // in the app's language; re-pushed when the language changes.
    void setLabels(const QJsonObject &labels);

    // --- Events pushed to connected pages --------------------------------------
    void showCaption(quint64 id, const QString &text, const QString &voiceName);
    // Real playback position (~15 Hz while a caption plays). fraction is the
    // same 0..1 the app's own "now speaking" line uses (SpeechQueue::progressFraction).
    void sendProgress(quint64 id, double fraction, qint64 playedMs, qint64 totalMs, bool totalKnown);
    // Mouth (0..1) and shape; only avatar pages and "wave" indicators get it.
    // Throttled to 20 Hz with a small dead band, always ends with a 0.
    void sendLevel(double mouth, const QString &viseme = QString());
    void setTalking(bool talking); // avatar pages: mouth open/closed image (with hysteresis)
    void setMicLive(bool live);
    // Chat pages only: a Twitch message that is being read aloud.
    // Fields: id, name, color ("#rrggbb" or ""), text, badges ([...]), action (bool).
    void chatMessage(const QJsonObject &message);
    void chatDelete(const QString &messageId); // a moderator deleted it
    void chatClearUser(const QString &login);   // timeout / ban
    void chatClear();
    void endCaption(quint64 id);
    void clearCaptions();
    void setSpeaking(bool speaking);
    void setListening(bool listening);

signals:
    void clientCountChanged(int count);

private:
    void onNewConnection();
    void onWebSocketConnection();
    void handleHttp(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                 bool headOnly = false);
    void broadcast(const QByteArray &json);
    bool isAllowedHost(const QByteArray &host) const;
    bool isAllowedOrigin(const QByteArray &origin, const QByteArray &host) const;
    QByteArray stateJson() const;

    QList<OverlayProfile> m_profiles;
    QHash<QString, QString> m_assets;
    QString m_fontDir = QStringLiteral(":/fonts");
    QStringList m_allowedHosts;
    QJsonObject m_labels;
    QString m_legacyQuery;
    QTcpServer *m_http = nullptr;
    QWebSocketServer *m_ws = nullptr;
    QList<QPointer<QWebSocket>> m_clients;
    QString m_error;
    QByteArray m_lastCaption; // replayed to pages that connect mid-sentence
    quint64 m_lastCaptionId = 0;
    bool m_allowLan = false;
    bool m_speaking = false;
    bool m_listening = false;
};
