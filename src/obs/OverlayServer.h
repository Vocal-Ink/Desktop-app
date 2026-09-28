#pragma once

#include "obs/OverlayStyle.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

class QTimer;
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

    // The fonts pages can load: [{family, url, weight, style}] (from the font dir).
    QJsonArray fonts() const { return m_fonts; }
    // "<version>+<hash of the page files>"; pages reload when it changes.
    QString build() const { return m_build; }

    static constexpr int kMaxPages = 32;
    static constexpr int kChatReplay = 20;

signals:
    void clientCountChanged(int count);

private:
    struct Client
    {
        QPointer<QWebSocket> socket;
        QString requested;     // ?profile= of the page ("" = main)
        QString kind;          // of the profile it gets (unknown ids get main)
        bool wantsLevel = false;
        QByteArray lastConfig; // last config sent, so unchanged configs aren't re-sent
    };
    struct ChatEntry
    {
        QJsonObject message;   // the {"type":"chat"} message as sent
        qint64 receivedMs = 0; // m_clock time
    };
    struct AwaitingChat
    {
        QString id;
        QStringList words;
        qint64 receivedMs = 0;
    };
    enum PageKinds { CaptionPages = 1, ChatPages = 2, AvatarPages = 4, LevelPages = 8, AllPages = 7 };

    void onNewConnection();
    void onWebSocketConnection();
    void handleHttp(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                 bool headOnly = false, const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
    void broadcast(const QByteArray &json);
    void sendTo(int pageKinds, const QByteArray &json);
    void refreshClients(); // re-resolve every page's profile, push configs that changed
    void updateClient(Client &client);
    void sendReplay(const Client &client);
    OverlayProfile profileFor(const QString &requested) const;
    QByteArray configJson(const OverlayProfile &profile) const;
    QByteArray avatarJson() const;
    void flushProgress();
    void flushLevel();
    void loadFonts();
    bool isAllowedHost(const QByteArray &host) const;
    bool isAllowedOrigin(const QByteArray &origin, const QByteArray &host) const;
    QByteArray stateJson() const;

    QList<OverlayProfile> m_profiles;
    QHash<QString, QString> m_assets;
    QString m_fontDir = QStringLiteral(":/fonts");
    QHash<QString, QString> m_fontFiles; // served file name -> path
    QJsonArray m_fonts;
    QStringList m_allowedHosts;          // lower case
    QStringList m_ownHostNames;          // this machine's names (LAN mode)
    QJsonObject m_labels;
    QString m_legacyQuery;
    QString m_build;
    QTcpServer *m_http = nullptr;
    QWebSocketServer *m_ws = nullptr;
    QList<Client> m_clients;
    QString m_error;
    QElapsedTimer m_clock;
    QByteArray m_lastCaption; // replayed to pages that connect mid-sentence
    quint64 m_lastCaptionId = 0;
    QString m_lastCaptionChat;           // id of the chat message the caption reads
    QStringList m_lastCaptionWords;
    QList<ChatEntry> m_chat;             // last kChatReplay, replayed to chat pages
    QList<AwaitingChat> m_chatAwaiting;  // shown on chat pages, not read yet
    quint64 m_chatCounter = 0;
    QTimer *m_progressTimer = nullptr;   // progress: at most ~15 per second
    QByteArray m_progressPending;
    quint64 m_progressId = 0;
    qint64 m_progressSentMs = -1;
    QTimer *m_levelTimer = nullptr;      // level: <= 20 Hz, dead band, always ends with 0
    double m_levelPending = 0.0;
    QString m_visemePending;
    bool m_levelHasPending = false;
    double m_levelSent = 0.0;
    QString m_visemeSent;
    qint64 m_levelSentMs = -1;
    bool m_allowLan = false;
    bool m_speaking = false;
    bool m_listening = false;
    bool m_talking = false;
    bool m_micLive = false;
};
