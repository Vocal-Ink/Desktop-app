#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class Settings;

// One overlay a streamer adds to OBS: captions, chat read aloud, or the
// built-in PNGtuber. Pages pick theirs with /?profile=<id>.
struct OverlayProfile
{
    QString id;        // url-safe ([a-z0-9-], 1..32), "main" always exists
    QString name;      // shown in the app
    QString kind;      // "captions" | "chat" | "avatar"
    QJsonObject style; // docs/overlay-style.md; missing fields take defaults
};

// The overlay style model (docs/overlay-style.md is the reference). The app's
// editor and the overlay page both work on the same JSON; the server sends
// pages the *effective* style: defaults(kind) <- preset(style.preset) <- style.
namespace OverlayStyle {

QStringList kinds();       // captions, chat, avatar
QStringList presetNames(); // subtitles, ink, bubble, outline, karaoke, typewriter, neon, lowerthird, minimal
QJsonObject defaults(const QString &kind);
QJsonObject preset(const QString &name); // partial style (only what the look changes)

// Deep merge: every key of `over` replaces the one in `base`; objects merge.
QJsonObject merge(const QJsonObject &base, const QJsonObject &over);
// defaults <- preset <- style, then every value clamped/validated (bad colours
// fall back to the default, numbers are clamped to their range, unknown
// enum values dropped, customCss sanitized).
QJsonObject effective(const QJsonObject &style, const QString &kind);

bool isValidColor(const QString &color); // #rgb, #rrggbb, #rrggbbaa
// Removes anything that could load or run something (@import, url() other than
// /fonts/ and /assets/, expression(), behavior:, -moz-binding, </style>...) and
// caps the length. The page scopes what remains under #vi-root.
QString sanitizeCss(const QString &css);

// Legacy URL options ("style=ink&size=48&color=%23ffffff...") as a style object.
QJsonObject fromQuery(const QString &query);

QList<OverlayProfile> parseProfiles(const QString &json);
QString serializeProfiles(const QList<OverlayProfile> &profiles);
// Profiles from Keys::OverlayProfiles. On first use the legacy Keys::OverlayQuery
// becomes profile "main" (and is saved back). Always contains "main".
QList<OverlayProfile> loadProfiles(Settings *settings);
void saveProfiles(Settings *settings, const QList<OverlayProfile> &profiles);
QString makeId(const QString &name, const QList<OverlayProfile> &existing);

// Avatar images: copies `sourceFile` into `assetDir` as <16 hex of its SHA-256>.<ext>
// after checking the real format (png, gif, webp, jpg; no SVG), size (<= 10 MB,
// <= 4096 px) and that it isn't a symlink. Returns the asset id ("" and
// `error` on failure).
QString importAsset(const QString &sourceFile, const QString &assetDir, QString *error = nullptr);
bool isAssetId(const QString &id);                   // ^[0-9a-f]{16}\.(png|gif|webp|jpg)$
QHash<QString, QString> scanAssets(const QString &assetDir); // id -> absolute path

} // namespace OverlayStyle
