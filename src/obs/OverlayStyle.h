#pragma once

#include <QByteArray>
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
// What a preset changes on a profile of `kind`. Captions get the whole preset;
// chat and avatar profiles keep their own layout (font size, width, alignment,
// position, name) and only take the look (font family and weight, colours, box
// shape, effects, enter/exit animation). The editor should reset exactly these
// fields when a preset is chosen for such a profile.
QJsonObject presetForKind(const QString &name, const QString &kind);

// Deep merge: every key of `over` replaces the one in `base`; objects merge.
QJsonObject merge(const QJsonObject &base, const QJsonObject &over);
// defaults <- preset <- style, then every value clamped/validated (bad colours
// fall back to the preset's or default value, numbers are clamped to their
// range, unknown enum values dropped, customCss sanitized). Unknown keys and
// sections that don't belong to `kind` are left out.
QJsonObject effective(const QJsonObject &style, const QString &kind);

bool isValidColor(const QString &color); // "#rgb" or "#rrggbb"
// Removes anything that could load or run something (@import, url() other than
// /fonts/ and /assets/, image-set(), expression(), behavior, -moz-binding,
// javascript:, "</"...) and caps the length at 8000 characters. The page adds
// what remains after its own CSS.
QString sanitizeCss(const QString &css);

// Legacy URL options ("style=ink&size=48&color=ffffff...") as a (validated,
// partial) style object. Only the options present are mapped.
QJsonObject fromQuery(const QString &query);
QStringList queryOptionNames(); // the legacy style options, in documentation order

bool isProfileId(const QString &id); // ^[a-z0-9-]{1,32}$
// Invalid ids and duplicates are dropped, unknown kinds become "captions",
// "main" (if present) is moved to the front.
QList<OverlayProfile> parseProfiles(const QString &json);
QString serializeProfiles(const QList<OverlayProfile> &profiles);
// Profiles from Keys::OverlayProfiles. On first use the legacy Keys::OverlayQuery
// becomes profile "main" (saved back, and remembered as Keys::OverlayLegacyQuery;
// Keys::OverlayQuery itself is left alone). Always contains "main", first.
QList<OverlayProfile> loadProfiles(Settings *settings);
void saveProfiles(Settings *settings, const QList<OverlayProfile> &profiles);
QString makeId(const QString &name, const QList<OverlayProfile> &existing);

// Avatar images: copies `sourceFile` into `assetDir` as <16 hex of its SHA-256>.<ext>
// after checking the real format (png, gif, webp, jpg; no SVG), size (<= 10 MB,
// <= 4096 px) and that it isn't a symlink. Importing the same image twice
// returns the same id. Returns the asset id ("" and `error` on failure).
QString importAsset(const QString &sourceFile, const QString &assetDir, QString *error = nullptr);
bool isAssetId(const QString &id);                   // ^[0-9a-f]{16}\.(png|gif|webp|jpg)$
QHash<QString, QString> scanAssets(const QString &assetDir); // id -> absolute path
// The image format from the bytes themselves (never the file name):
// "png", "gif", "webp", "jpg", or "" for anything else (SVG included).
QString imageFormat(const QByteArray &data);
inline constexpr qint64 kMaxAssetBytes = 10 * 1024 * 1024;
inline constexpr int kMaxAssetPixels = 4096;
inline constexpr int kMaxCustomCss = 8000;

} // namespace OverlayStyle
