#include "obs/OverlayStyle.h"

#include "core/Settings.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSize>
#include <QUrlQuery>
#include <cmath>
#include <optional>

// The style model of docs/overlay-style.md: defaults per kind, the presets,
// validation, the legacy URL options and the avatar image store.

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("OverlayStyle", text);
}

QJsonObject jsonObject(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

// --- Schema ------------------------------------------------------------------

enum class Type { Int, Real, Bool, Enum, Color, Text, Family, Asset, Css };

struct Field
{
    const char *path;
    Type type;
    double lo;          // numbers: range; Text/Family: maximum length
    double hi;
    const char *values; // Enum: "a|b|c"
    const char *kind;   // nullptr: every kind; else the only kind that has it
};

constexpr const char *kAnchors = "top-left|top|top-right|left|center|right|bottom-left|bottom|bottom-right";

const Field kFields[] = {
    {"preset", Type::Enum, 0, 0, "subtitles|ink|bubble|outline|karaoke|typewriter|neon|lowerthird|minimal", nullptr},

    {"font.family", Type::Family, 0, 100, nullptr, nullptr},
    {"font.size", Type::Int, 12, 160, nullptr, nullptr},
    {"font.weight", Type::Int, 300, 900, nullptr, nullptr},
    {"font.letterSpacing", Type::Real, -5, 30, nullptr, nullptr},
    {"font.lineHeight", Type::Real, 0.9, 2.0, nullptr, nullptr},
    {"font.uppercase", Type::Bool, 0, 0, nullptr, nullptr},
    {"font.italic", Type::Bool, 0, 0, nullptr, nullptr},

    {"colors.text", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.ink", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.unspoken", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.unspokenOpacity", Type::Int, 0, 100, nullptr, nullptr},
    {"colors.background", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.backgroundOpacity", Type::Int, 0, 100, nullptr, nullptr},
    {"colors.border", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.borderOpacity", Type::Int, 0, 100, nullptr, nullptr},
    {"colors.outline", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.shadow", Type::Color, 0, 0, nullptr, nullptr},
    {"colors.name", Type::Color, 0, 0, nullptr, nullptr},

    {"box.padding", Type::Int, 0, 80, nullptr, nullptr},
    {"box.radius", Type::Int, 0, 60, nullptr, nullptr},
    {"box.borderWidth", Type::Int, 0, 12, nullptr, nullptr},
    {"box.shadow", Type::Int, 0, 100, nullptr, nullptr},
    {"box.maxWidth", Type::Int, 20, 100, nullptr, nullptr},
    {"box.align", Type::Enum, 0, 0, "left|center|right", nullptr},
    {"box.tail", Type::Enum, 0, 0, "none|left|center|right", nullptr},

    {"effects.outline", Type::Int, 0, 16, nullptr, nullptr},
    {"effects.textShadow", Type::Int, 0, 100, nullptr, nullptr},
    {"effects.glow", Type::Int, 0, 100, nullptr, nullptr},

    {"position.anchor", Type::Enum, 0, 0, kAnchors, nullptr},
    {"position.offsetX", Type::Real, -50, 50, nullptr, nullptr},
    {"position.offsetY", Type::Real, -50, 50, nullptr, nullptr},
    {"position.margin", Type::Int, 0, 200, nullptr, nullptr},

    {"animation.enter", Type::Enum, 0, 0, "none|fade|rise|pop|slide", nullptr},
    {"animation.word", Type::Enum, 0, 0, "none|fade|rise|pop|ink|glow|bounce|type", nullptr},
    {"animation.exit", Type::Enum, 0, 0, "none|fade|sink|shrink", nullptr},
    {"animation.speed", Type::Int, 25, 300, nullptr, nullptr},

    {"timing.reveal", Type::Enum, 0, 0, "audio|estimate|instant", nullptr},
    {"timing.wps", Type::Real, 1, 8, nullptr, nullptr},
    {"timing.hold", Type::Real, -1, 60, nullptr, nullptr},

    {"history.lines", Type::Int, 1, 6, nullptr, nullptr},
    {"history.roll", Type::Bool, 0, 0, nullptr, nullptr},
    {"history.maxLines", Type::Int, 1, 10, nullptr, nullptr},

    {"name.show", Type::Bool, 0, 0, nullptr, nullptr},
    {"name.position", Type::Enum, 0, 0, "above|inline|below", nullptr},
    {"name.text", Type::Text, 0, 60, nullptr, nullptr},

    {"indicator.show", Type::Bool, 0, 0, nullptr, nullptr},
    {"indicator.style", Type::Enum, 0, 0, "dot|bars|wave", nullptr},
    {"indicator.position", Type::Enum, 0, 0, "before|after|corner", nullptr},

    {"customCss", Type::Css, 0, 0, nullptr, nullptr},

    {"chat.maxMessages", Type::Int, 1, 20, nullptr, "chat"},
    {"chat.showBadges", Type::Bool, 0, 0, nullptr, "chat"},
    {"chat.useNameColors", Type::Bool, 0, 0, nullptr, "chat"},
    {"chat.fadeAfter", Type::Int, 0, 300, nullptr, "chat"},
    {"chat.direction", Type::Enum, 0, 0, "up|down", "chat"},
    {"chat.highlightReading", Type::Bool, 0, 0, nullptr, "chat"},

    {"avatar.images.idle", Type::Asset, 0, 0, nullptr, "avatar"},
    {"avatar.images.talking", Type::Asset, 0, 0, nullptr, "avatar"},
    {"avatar.images.blink", Type::Asset, 0, 0, nullptr, "avatar"},
    {"avatar.images.talkingBlink", Type::Asset, 0, 0, nullptr, "avatar"},
    {"avatar.images.micLive", Type::Asset, 0, 0, nullptr, "avatar"},
    {"avatar.threshold", Type::Int, 1, 50, nullptr, "avatar"},
    {"avatar.motion", Type::Enum, 0, 0, "none|bounce|squash|shake|float", "avatar"},
    {"avatar.motionOnlyWhileTalking", Type::Bool, 0, 0, nullptr, "avatar"},
    {"avatar.intensity", Type::Int, 0, 100, nullptr, "avatar"},
    {"avatar.blinkEvery", Type::Real, 0, 20, nullptr, "avatar"},
    {"avatar.flip", Type::Bool, 0, 0, nullptr, "avatar"},
    {"avatar.size", Type::Int, 10, 100, nullptr, "avatar"},
    {"avatar.shadow", Type::Int, 0, 100, nullptr, "avatar"},
    {"avatar.micLiveGlow", Type::Bool, 0, 0, nullptr, "avatar"},
    {"avatar.dimWhenIdle", Type::Int, 0, 100, nullptr, "avatar"},
    {"avatar.anchor", Type::Enum, 0, 0, kAnchors, "avatar"},
};

// Defaults shared by every kind (the "subtitles" look, bottom centre).
const char kCommonDefaults[] = R"({
  "preset": "subtitles",
  "font": { "family": "Bricolage Grotesque", "size": 44, "weight": 700, "letterSpacing": 0,
            "lineHeight": 1.25, "uppercase": false, "italic": false },
  "colors": { "text": "#ffffff", "ink": "#b48cff", "unspoken": "#ffffff", "unspokenOpacity": 35,
              "background": "#120d1f", "backgroundOpacity": 72, "border": "#ffffff", "borderOpacity": 12,
              "outline": "#000000", "shadow": "#000000", "name": "#d9c6ff" },
  "box": { "padding": 18, "radius": 14, "borderWidth": 0, "shadow": 30, "maxWidth": 70,
           "align": "center", "tail": "none" },
  "effects": { "outline": 0, "textShadow": 40, "glow": 0 },
  "position": { "anchor": "bottom", "offsetX": 0, "offsetY": 0, "margin": 48 },
  "animation": { "enter": "rise", "word": "ink", "exit": "fade", "speed": 100 },
  "timing": { "reveal": "audio", "wps": 2.6, "hold": 4 },
  "history": { "lines": 1, "roll": false, "maxLines": 3 },
  "name": { "show": false, "position": "above", "text": "" },
  "indicator": { "show": false, "style": "dot", "position": "before" },
  "customCss": ""
})";

// A column of message cards at the bottom left.
const char kChatDefaults[] = R"({
  "font": { "size": 28 },
  "box": { "padding": 14, "maxWidth": 30, "align": "left" },
  "position": { "anchor": "bottom-left" },
  "animation": { "word": "none" },
  "name": { "show": true, "position": "above" },
  "chat": { "maxMessages": 5, "showBadges": true, "useNameColors": true, "fadeAfter": 30,
            "direction": "up", "highlightReading": true }
})";

const char kAvatarDefaults[] = R"({
  "avatar": {
    "images": { "idle": "", "talking": "", "blink": "", "talkingBlink": "", "micLive": "" },
    "threshold": 8, "motion": "bounce", "motionOnlyWhileTalking": true, "intensity": 60,
    "blinkEvery": 4, "flip": false, "size": 60, "shadow": 0, "micLiveGlow": true,
    "dimWhenIdle": 0, "anchor": "bottom-left"
  }
})";

// Every preset sets the whole look (font, colours, box, effects, enter/word/exit
// animation), so switching presets never leaves half of the previous one behind.
struct PresetDef
{
    const char *name;
    const char *json;
};

const PresetDef kPresets[] = {
    {"subtitles", R"({
      "font": { "family": "Bricolage Grotesque", "size": 44, "weight": 700, "letterSpacing": 0,
                "lineHeight": 1.25, "uppercase": false, "italic": false },
      "colors": { "text": "#ffffff", "ink": "#b48cff", "unspoken": "#ffffff", "unspokenOpacity": 35,
                  "background": "#120d1f", "backgroundOpacity": 72, "border": "#ffffff", "borderOpacity": 12,
                  "outline": "#000000", "shadow": "#000000", "name": "#d9c6ff" },
      "box": { "padding": 18, "radius": 14, "borderWidth": 0, "shadow": 30, "maxWidth": 70,
               "align": "center", "tail": "none" },
      "effects": { "outline": 0, "textShadow": 40, "glow": 0 },
      "animation": { "enter": "rise", "word": "ink", "exit": "fade" } })"},
    {"ink", R"({
      "font": { "family": "Vocal Ink Display", "size": 56, "weight": 800, "letterSpacing": 0,
                "lineHeight": 1.18, "uppercase": false, "italic": false },
      "colors": { "text": "#f4eeff", "ink": "#b48cff", "unspoken": "#ffffff", "unspokenOpacity": 40,
                  "background": "#120d1f", "backgroundOpacity": 0, "border": "#ffffff", "borderOpacity": 0,
                  "outline": "#150b29", "shadow": "#07030f", "name": "#d9c6ff" },
      "box": { "padding": 8, "radius": 0, "borderWidth": 0, "shadow": 0, "maxWidth": 72,
               "align": "center", "tail": "none" },
      "effects": { "outline": 3, "textShadow": 70, "glow": 0 },
      "animation": { "enter": "rise", "word": "ink", "exit": "fade" } })"},
    {"bubble", R"({
      "font": { "family": "Bricolage Grotesque", "size": 40, "weight": 700, "letterSpacing": 0,
                "lineHeight": 1.3, "uppercase": false, "italic": false },
      "colors": { "text": "#1d1830", "ink": "#7c4dff", "unspoken": "#1d1830", "unspokenOpacity": 25,
                  "background": "#ffffff", "backgroundOpacity": 100, "border": "#1d1830", "borderOpacity": 0,
                  "outline": "#000000", "shadow": "#0d0820", "name": "#7c4dff" },
      "box": { "padding": 24, "radius": 30, "borderWidth": 0, "shadow": 45, "maxWidth": 40,
               "align": "left", "tail": "left" },
      "effects": { "outline": 0, "textShadow": 0, "glow": 0 },
      "position": { "anchor": "top-left", "offsetX": 14, "offsetY": 10 },
      "animation": { "enter": "pop", "word": "rise", "exit": "shrink" } })"},
    {"outline", R"({
      "font": { "family": "Vocal Ink Display", "size": 66, "weight": 800, "letterSpacing": 1.5,
                "lineHeight": 1.12, "uppercase": true, "italic": false },
      "colors": { "text": "#ffffff", "ink": "#ffd43b", "unspoken": "#ffffff", "unspokenOpacity": 0,
                  "background": "#000000", "backgroundOpacity": 0, "border": "#ffffff", "borderOpacity": 0,
                  "outline": "#000000", "shadow": "#000000", "name": "#ffd43b" },
      "box": { "padding": 0, "radius": 0, "borderWidth": 0, "shadow": 0, "maxWidth": 80,
               "align": "center", "tail": "none" },
      "effects": { "outline": 7, "textShadow": 55, "glow": 0 },
      "animation": { "enter": "pop", "word": "pop", "exit": "shrink" } })"},
    {"karaoke", R"({
      "font": { "family": "Lexend", "size": 50, "weight": 600, "letterSpacing": 0,
                "lineHeight": 1.3, "uppercase": false, "italic": false },
      "colors": { "text": "#ffffff", "ink": "#35d6ff", "unspoken": "#ffffff", "unspokenOpacity": 100,
                  "background": "#0b0718", "backgroundOpacity": 0, "border": "#ffffff", "borderOpacity": 0,
                  "outline": "#150c2e", "shadow": "#000000", "name": "#35d6ff" },
      "box": { "padding": 8, "radius": 0, "borderWidth": 0, "shadow": 0, "maxWidth": 80,
               "align": "center", "tail": "none" },
      "effects": { "outline": 5, "textShadow": 45, "glow": 0 },
      "animation": { "enter": "fade", "word": "ink", "exit": "fade" } })"},
    {"typewriter", R"({
      "font": { "family": "Atkinson Hyperlegible Mono", "size": 36, "weight": 500, "letterSpacing": 0,
                "lineHeight": 1.45, "uppercase": false, "italic": false },
      "colors": { "text": "#221d17", "ink": "#d9480f", "unspoken": "#221d17", "unspokenOpacity": 0,
                  "background": "#f8f2e4", "backgroundOpacity": 100, "border": "#221d17", "borderOpacity": 14,
                  "outline": "#000000", "shadow": "#1c140a", "name": "#9a3412" },
      "box": { "padding": 24, "radius": 6, "borderWidth": 1, "shadow": 45, "maxWidth": 62,
               "align": "left", "tail": "none" },
      "effects": { "outline": 0, "textShadow": 0, "glow": 0 },
      "animation": { "enter": "fade", "word": "type", "exit": "fade" } })"},
    {"neon", R"({
      "font": { "family": "Lexend", "size": 52, "weight": 600, "letterSpacing": 1,
                "lineHeight": 1.3, "uppercase": false, "italic": false },
      "colors": { "text": "#fff4fd", "ink": "#ff3bd4", "unspoken": "#ffffff", "unspokenOpacity": 0,
                  "background": "#0a0612", "backgroundOpacity": 0, "border": "#ff3bd4", "borderOpacity": 0,
                  "outline": "#000000", "shadow": "#000000", "name": "#7af7ff" },
      "box": { "padding": 12, "radius": 0, "borderWidth": 0, "shadow": 0, "maxWidth": 75,
               "align": "center", "tail": "none" },
      "effects": { "outline": 0, "textShadow": 0, "glow": 85 },
      "animation": { "enter": "fade", "word": "glow", "exit": "fade" } })"},
    {"lowerthird", R"({
      "font": { "family": "Atkinson Hyperlegible Next", "size": 36, "weight": 600, "letterSpacing": 0,
                "lineHeight": 1.32, "uppercase": false, "italic": false },
      "colors": { "text": "#ffffff", "ink": "#b48cff", "unspoken": "#ffffff", "unspokenOpacity": 35,
                  "background": "#120d1f", "backgroundOpacity": 92, "border": "#b48cff", "borderOpacity": 100,
                  "outline": "#000000", "shadow": "#000000", "name": "#120d1f" },
      "box": { "padding": 20, "radius": 4, "borderWidth": 0, "shadow": 40, "maxWidth": 52,
               "align": "left", "tail": "none" },
      "effects": { "outline": 0, "textShadow": 0, "glow": 0 },
      "position": { "anchor": "bottom-left", "offsetX": 0, "offsetY": 0, "margin": 64 },
      "name": { "show": true, "position": "above" },
      "animation": { "enter": "slide", "word": "fade", "exit": "fade" } })"},
    {"minimal", R"({
      "font": { "family": "Atkinson Hyperlegible Next", "size": 30, "weight": 600, "letterSpacing": 0,
                "lineHeight": 1.3, "uppercase": false, "italic": false },
      "colors": { "text": "#ffffff", "ink": "#ffffff", "unspoken": "#ffffff", "unspokenOpacity": 0,
                  "background": "#000000", "backgroundOpacity": 0, "border": "#ffffff", "borderOpacity": 0,
                  "outline": "#000000", "shadow": "#000000", "name": "#ffffff" },
      "box": { "padding": 4, "radius": 0, "borderWidth": 0, "shadow": 0, "maxWidth": 60,
               "align": "center", "tail": "none" },
      "effects": { "outline": 0, "textShadow": 35, "glow": 0 },
      "animation": { "enter": "fade", "word": "none", "exit": "fade" } })"},
};

// --- JSON paths --------------------------------------------------------------

QJsonValue lookup(const QJsonObject &object, const QString &path)
{
    const qsizetype dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0)
        return object.value(path);
    const QJsonValue child = object.value(path.left(dot));
    return child.isObject() ? lookup(child.toObject(), path.mid(dot + 1)) : QJsonValue(QJsonValue::Undefined);
}

void assign(QJsonObject &object, const QString &path, const QJsonValue &value)
{
    const qsizetype dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        object.insert(path, value);
        return;
    }
    const QString key = path.left(dot);
    QJsonObject child = object.value(key).toObject();
    assign(child, path.mid(dot + 1), value);
    object.insert(key, child);
}

void removePath(QJsonObject &object, const QString &path)
{
    const qsizetype dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        object.remove(path);
        return;
    }
    const QString key = path.left(dot);
    if (!object.value(key).isObject())
        return;
    QJsonObject child = object.value(key).toObject();
    removePath(child, path.mid(dot + 1));
    if (child.isEmpty())
        object.remove(key);
    else
        object.insert(key, child);
}

// --- Value checks ------------------------------------------------------------

// Like JavaScript's parseFloat: "48px" -> 48.
std::optional<double> leadingNumber(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double v = m.captured(1).toDouble(&ok);
    return ok && std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
}

std::optional<double> toNumber(const QJsonValue &v)
{
    if (v.isDouble())
        return std::isfinite(v.toDouble()) ? std::optional<double>(v.toDouble()) : std::nullopt;
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        if (ok && std::isfinite(d))
            return d;
    }
    return std::nullopt;
}

std::optional<bool> toBool(const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        if (s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes") || s == QLatin1String("on"))
            return true;
        if (s == QLatin1String("0") || s == QLatin1String("false") || s == QLatin1String("no") || s == QLatin1String("off")
            || s.isEmpty())
            return false;
    }
    return std::nullopt;
}

QString withoutControls(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() >= 0x20 && c.unicode() != 0x7f)
            out += c;
    }
    return out;
}

// "#abc" / "#aabbcc" (any case) -> "#aabbcc"; anything else -> "".
QString normalizeColor(const QString &color)
{
    const QString c = color.trimmed();
    if (!OverlayStyle::isValidColor(c))
        return {};
    if (c.size() == 4) {
        QString out(QStringLiteral("#"));
        for (int i = 1; i < 4; ++i)
            out += QString(2, c.at(i));
        return out.toLower();
    }
    return c.toLower();
}

std::optional<QJsonValue> normalize(const Field &field, const QJsonValue &v)
{
    if (v.isUndefined() || v.isNull())
        return std::nullopt;
    switch (field.type) {
    case Type::Int:
    case Type::Real: {
        const std::optional<double> n = toNumber(v);
        if (!n)
            return std::nullopt;
        double d = *n;
        if (field.lo < 0 && qstrcmp(field.path, "timing.hold") == 0 && d < 0)
            d = -1; // "until the next caption"
        d = qBound(field.lo, d, field.hi);
        if (field.type == Type::Int)
            return QJsonValue(qRound(d));
        return QJsonValue(std::round(d * 100.0) / 100.0);
    }
    case Type::Bool: {
        const std::optional<bool> b = toBool(v);
        return b ? std::optional<QJsonValue>(QJsonValue(*b)) : std::nullopt;
    }
    case Type::Enum: {
        if (!v.isString())
            return std::nullopt;
        const QString s = v.toString().trimmed().toLower();
        const QStringList allowed = QString::fromLatin1(field.values).split(QLatin1Char('|'));
        // Enum values are compared case-insensitively but stored as documented.
        for (const QString &a : allowed) {
            if (a.toLower() == s)
                return QJsonValue(a);
        }
        return std::nullopt;
    }
    case Type::Color: {
        const QString c = v.isString() ? normalizeColor(v.toString()) : QString();
        return c.isEmpty() ? std::nullopt : std::optional<QJsonValue>(QJsonValue(c));
    }
    case Type::Text: {
        if (!v.isString())
            return std::nullopt;
        return QJsonValue(withoutControls(v.toString()).trimmed().left(int(field.hi)));
    }
    case Type::Family: {
        if (!v.isString())
            return std::nullopt;
        // Goes into a CSS font-family list: no quotes, escapes or anything that
        // could end the declaration. Commas separate fallbacks.
        QString s = withoutControls(v.toString());
        static const QRegularExpression unsafe(QStringLiteral("[\"'\\\\;{}<>()/:@!*]"));
        s.remove(unsafe);
        s = s.simplified().left(int(field.hi)).trimmed();
        while (s.endsWith(QLatin1Char(',')))
            s.chop(1);
        return s.isEmpty() ? std::nullopt : std::optional<QJsonValue>(QJsonValue(s));
    }
    case Type::Asset: {
        if (!v.isString())
            return std::nullopt;
        const QString s = v.toString().trimmed();
        return s.isEmpty() || OverlayStyle::isAssetId(s) ? std::optional<QJsonValue>(QJsonValue(s)) : std::nullopt;
    }
    case Type::Css:
        if (!v.isString())
            return std::nullopt;
        return QJsonValue(OverlayStyle::sanitizeCss(v.toString()));
    }
    return std::nullopt;
}

bool fieldApplies(const Field &field, const QString &kind)
{
    return !field.kind || kind == QLatin1String(field.kind);
}

// Keeps only the fields that are valid (normalized); nothing is filled in.
QJsonObject validatePartial(const QJsonObject &partial)
{
    QJsonObject out;
    for (const Field &f : kFields) {
        const QString path = QString::fromLatin1(f.path);
        const std::optional<QJsonValue> v = normalize(f, lookup(partial, path));
        if (v)
            assign(out, path, *v);
    }
    return out;
}

// --- Legacy URL options ------------------------------------------------------

struct LegacyColor
{
    QString hex;     // "#rrggbb"
    int opacity = 100;
};

std::optional<LegacyColor> legacyColor(const QString &value)
{
    QString v = value.trimmed();
    if (v.isEmpty())
        return std::nullopt;
    if (v.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0
        || v.compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0)
        return LegacyColor{QStringLiteral("#000000"), 0};

    // "#" starts the URL fragment, so the old page took hex without it.
    static const QRegularExpression hex(QStringLiteral("^#?([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const QRegularExpressionMatch h = hex.match(v);
    if (h.hasMatch()) {
        QString digits = h.captured(1).toLower();
        if (digits.size() <= 4) {
            QString wide;
            for (const QChar c : digits)
                wide += QString(2, c);
            digits = wide;
        }
        LegacyColor out{QLatin1Char('#') + digits.left(6), 100};
        if (digits.size() == 8)
            out.opacity = qRound(digits.mid(6, 2).toInt(nullptr, 16) * 100.0 / 255.0);
        return out;
    }

    // rgb(0, 0, 0) / rgba(0,0,0,0.6) / rgb(0 0 0 / 60%)
    static const QRegularExpression rgb(
        QStringLiteral("^rgba?\\(\\s*([\\d.]+%?)[\\s,]+([\\d.]+%?)[\\s,]+([\\d.]+%?)(?:\\s*[,/]\\s*([\\d.]+%?))?\\s*\\)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rgb.match(v);
    if (m.hasMatch()) {
        const auto channel = [](const QString &s) {
            const double d = s.endsWith(QLatin1Char('%')) ? s.chopped(1).toDouble() * 2.55 : s.toDouble();
            return qBound(0, qRound(d), 255);
        };
        LegacyColor out;
        out.hex = QColor(channel(m.captured(1)), channel(m.captured(2)), channel(m.captured(3))).name();
        if (!m.captured(4).isEmpty()) {
            const QString a = m.captured(4);
            const double alpha = a.endsWith(QLatin1Char('%')) ? a.chopped(1).toDouble() / 100.0 : a.toDouble();
            out.opacity = qBound(0, qRound(alpha * 100.0), 100);
        }
        return out;
    }

    // CSS colour names ("white", "gold"...).
    static const QRegularExpression name(QStringLiteral("^[A-Za-z]{3,30}$"));
    if (name.match(v).hasMatch()) {
        const QColor c = QColor::fromString(v);
        if (c.isValid())
            return LegacyColor{c.name(), qRound(c.alphaF() * 100.0)};
    }
    return std::nullopt;
}

bool legacyFlag(const QString &value)
{
    static const QRegularExpression on(QStringLiteral("^(1|true|yes|on)$"), QRegularExpression::CaseInsensitiveOption);
    return value.trimmed().isEmpty() || on.match(value.trimmed()).hasMatch();
}

// --- Images -----------------------------------------------------------------

quint32 be16(const QByteArray &d, qsizetype i)
{
    return (quint32(quint8(d.at(i))) << 8) | quint8(d.at(i + 1));
}

quint32 le16(const QByteArray &d, qsizetype i)
{
    return quint8(d.at(i)) | (quint32(quint8(d.at(i + 1))) << 8);
}

quint32 le24(const QByteArray &d, qsizetype i)
{
    return le16(d, i) | (quint32(quint8(d.at(i + 2))) << 16);
}

// Format from the magic bytes only.
QString sniffFormat(const QByteArray &d)
{
    if (d.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral("png");
    if (d.startsWith("GIF87a") || d.startsWith("GIF89a"))
        return QStringLiteral("gif");
    if (d.size() >= 3 && quint8(d.at(0)) == 0xff && quint8(d.at(1)) == 0xd8 && quint8(d.at(2)) == 0xff)
        return QStringLiteral("jpg");
    if (d.size() >= 16 && d.startsWith("RIFF") && d.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    return {};
}

// Width and height from the header, for when Qt has no reader for the format
// (the WebP plugin is optional).
QSize headerSize(const QByteArray &d, const QString &format)
{
    if (format == QLatin1String("png") && d.size() >= 24 && d.mid(12, 4) == "IHDR")
        return QSize(int((be16(d, 16) << 16) | be16(d, 18)), int((be16(d, 20) << 16) | be16(d, 22)));
    if (format == QLatin1String("gif") && d.size() >= 10)
        return QSize(int(le16(d, 6)), int(le16(d, 8)));
    if (format == QLatin1String("webp") && d.size() >= 30) {
        const QByteArray chunk = d.mid(12, 4);
        if (chunk == "VP8X")
            return QSize(int(le24(d, 24)) + 1, int(le24(d, 27)) + 1);
        if (chunk == "VP8L" && quint8(d.at(20)) == 0x2f) {
            const quint32 bits = le16(d, 21) | (le16(d, 23) << 16);
            return QSize(int(bits & 0x3fff) + 1, int((bits >> 14) & 0x3fff) + 1);
        }
        if (chunk == "VP8 " && quint8(d.at(23)) == 0x9d && quint8(d.at(24)) == 0x01 && quint8(d.at(25)) == 0x2a)
            return QSize(int(le16(d, 26) & 0x3fff), int(le16(d, 28) & 0x3fff));
        return {};
    }
    if (format == QLatin1String("jpg")) {
        qsizetype i = 2;
        while (i + 9 < d.size()) {
            if (quint8(d.at(i)) != 0xff)
                return {};
            const quint8 marker = quint8(d.at(i + 1));
            if (marker == 0xff) {
                ++i;
                continue;
            }
            const bool sof = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
            if (sof)
                return QSize(int(be16(d, i + 7)), int(be16(d, i + 5)));
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) {
                i += 2;
                continue;
            }
            i += 2 + be16(d, i + 2);
        }
    }
    return {};
}

bool inspectImage(const QByteArray &data, QString *format, QSize *size)
{
    QByteArray bytes = data;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    // What Qt itself makes of the content (never the file name).
    QString real = QString::fromLatin1(QImageReader::imageFormat(&buffer)).toLower();
    if (real == QLatin1String("jpeg"))
        real = QStringLiteral("jpg");
    const QString sniffed = sniffFormat(data);
    if (!real.isEmpty() && real != sniffed)
        return false; // svg, bmp, ico... or a mismatch
    if (sniffed.isEmpty())
        return false;
    *format = sniffed;

    QSize s;
    buffer.seek(0);
    QImageReader reader(&buffer);
    if (!real.isEmpty() && reader.canRead())
        s = reader.size();
    if (!s.isValid())
        s = headerSize(data, sniffed);
    *size = s;
    return s.isValid() && !s.isEmpty();
}

} // namespace

// --- Public API -------------------------------------------------------------

QStringList OverlayStyle::kinds()
{
    return {QStringLiteral("captions"), QStringLiteral("chat"), QStringLiteral("avatar")};
}

QStringList OverlayStyle::presetNames()
{
    QStringList names;
    for (const PresetDef &p : kPresets)
        names << QString::fromLatin1(p.name);
    return names;
}

QJsonObject OverlayStyle::defaults(const QString &kind)
{
    static const QJsonObject common = jsonObject(kCommonDefaults);
    static const QJsonObject chat = merge(common, jsonObject(kChatDefaults));
    static const QJsonObject avatar = merge(common, jsonObject(kAvatarDefaults));
    if (kind == QLatin1String("chat"))
        return chat;
    if (kind == QLatin1String("avatar"))
        return avatar;
    return common;
}

QJsonObject OverlayStyle::preset(const QString &name)
{
    static const QHash<QString, QJsonObject> presets = [] {
        QHash<QString, QJsonObject> out;
        for (const PresetDef &p : kPresets)
            out.insert(QString::fromLatin1(p.name), jsonObject(p.json));
        return out;
    }();
    return presets.value(name);
}

QJsonObject OverlayStyle::presetForKind(const QString &name, const QString &kind)
{
    QJsonObject p = preset(name);
    if (kind == QLatin1String("chat") || kind == QLatin1String("avatar")) {
        for (const char *path : {"font.size", "box.maxWidth", "box.align", "position", "name", "history", "timing",
                                 "animation.word"})
            removePath(p, QString::fromLatin1(path));
    }
    return p;
}

QJsonObject OverlayStyle::merge(const QJsonObject &base, const QJsonObject &over)
{
    QJsonObject out = base;
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it.value().isObject() && out.value(it.key()).isObject())
            out.insert(it.key(), merge(out.value(it.key()).toObject(), it.value().toObject()));
        else
            out.insert(it.key(), it.value());
    }
    return out;
}

QJsonObject OverlayStyle::effective(const QJsonObject &style, const QString &kind)
{
    const QString k = kinds().contains(kind) ? kind : QStringLiteral("captions");
    const Field &presetField = kFields[0];
    const std::optional<QJsonValue> chosen = normalize(presetField, style.value(QStringLiteral("preset")));
    const QString presetName = chosen ? chosen->toString() : QStringLiteral("subtitles");
    const QJsonObject base = merge(defaults(k), presetForKind(presetName, k));

    QJsonObject out;
    for (const Field &f : kFields) {
        if (!fieldApplies(f, k))
            continue;
        const QString path = QString::fromLatin1(f.path);
        std::optional<QJsonValue> v = normalize(f, lookup(style, path));
        if (!v)
            v = normalize(f, lookup(base, path));
        if (v)
            assign(out, path, *v);
    }
    out.insert(QStringLiteral("preset"), presetName);
    return out;
}

bool OverlayStyle::isValidColor(const QString &color)
{
    static const QRegularExpression re(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$"));
    return re.match(color).hasMatch();
}

QString OverlayStyle::sanitizeCss(const QString &css)
{
    QString s = css.left(kMaxCustomCss);
    // Control characters (NUL can hide things from simple filters).
    QString clean;
    clean.reserve(s.size());
    for (const QChar c : std::as_const(s)) {
        if (c.unicode() >= 0x20 || c == QLatin1Char('\n') || c == QLatin1Char('\t'))
            clean += c;
    }
    s = clean;

    static const QRegularExpression comments(QStringLiteral("/\\*.*?(?:\\*/|$)"),
                                             QRegularExpression::DotMatchesEverythingOption);
    s.replace(comments, QStringLiteral(" "));

    // CSS escapes can spell keywords ("@\69mport", "u\rl("): decode the ones
    // that stand for letters, digits or the punctuation the filters look for.
    static const QRegularExpression escape(QStringLiteral("\\\\(?:([0-9a-fA-F]{1,6})\\s?|([^0-9a-fA-F\\n]))"));
    const auto decodeEscapes = [](const QString &text) {
        QString out;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = escape.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            QChar c;
            if (m.capturedLength(1) > 0) {
                const uint code = m.captured(1).toUInt(nullptr, 16);
                if (code < 0x80)
                    c = QChar(char16_t(code));
            } else {
                c = m.captured(2).at(0);
            }
            const bool decode = !c.isNull() && c.unicode() < 0x80
                && (c.isLetterOrNumber() || QStringLiteral("@:()/.-_").contains(c));
            if (!decode)
                continue;
            out += QStringView(text).mid(last, m.capturedStart() - last);
            out += c;
            last = m.capturedEnd();
        }
        out += QStringView(text).mid(last);
        return out;
    };

    static const QRegularExpression import(QStringLiteral("@import[^;]*;?"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression url(QStringLiteral("url\\(\\s*([\"']?)([^\"')]*)\\1\\s*\\)"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression allowedUrl(QStringLiteral("^/(?:fonts|assets)/[A-Za-z0-9._-]+$"));
    static const QRegularExpression strayUrl(QStringLiteral("url\\("), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression banned(
        QStringLiteral("(?:-webkit-)?image-set|image\\(|cross-fade|expression|-ms-behavior|behavior|-moz-binding|"
                       "javascript\\s*:|vbscript\\s*:|</|<!--|-->"),
        QRegularExpression::CaseInsensitiveOption);

    for (int pass = 0; pass < 8; ++pass) {
        const QString before = s;
        s = decodeEscapes(s);
        s.remove(import);
        // url(): only the page's own fonts and images.
        QString out;
        qsizetype last = 0;
        QRegularExpressionMatchIterator it = url.globalMatch(s);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            out += QStringView(s).mid(last, m.capturedStart() - last);
            const QString target = m.captured(2).trimmed();
            const bool ok = allowedUrl.match(target).hasMatch() && !target.contains(QLatin1String(".."));
            out += ok ? QStringLiteral("url(\"") + target + QStringLiteral("\")") : QStringLiteral("none");
            last = m.capturedEnd();
        }
        out += QStringView(s).mid(last);
        s = out;
        // A url( that didn't parse as a whole (unbalanced quotes...) is dropped.
        {
            QString kept;
            qsizetype pos = 0;
            QRegularExpressionMatchIterator stray = strayUrl.globalMatch(s);
            while (stray.hasNext()) {
                const QRegularExpressionMatch m = stray.next();
                const QString rest = s.mid(m.capturedStart(), 200);
                if (rest.startsWith(QLatin1String("url(\"/fonts/")) || rest.startsWith(QLatin1String("url(\"/assets/")))
                    continue;
                kept += QStringView(s).mid(pos, m.capturedStart() - pos);
                pos = m.capturedEnd();
            }
            kept += QStringView(s).mid(pos);
            s = kept;
        }
        s.remove(banned);
        if (s == before)
            break;
    }
    return s.trimmed();
}

QStringList OverlayStyle::queryOptionNames()
{
    return {QStringLiteral("style"), QStringLiteral("position"), QStringLiteral("align"), QStringLiteral("tail"),
            QStringLiteral("font"), QStringLiteral("size"), QStringLiteral("color"), QStringLiteral("bg"),
            QStringLiteral("ink"), QStringLiteral("outline"), QStringLiteral("outlinecolor"), QStringLiteral("reveal"),
            QStringLiteral("wps"), QStringLiteral("hold"), QStringLiteral("lines"), QStringLiteral("width"),
            QStringLiteral("roll"), QStringLiteral("name"), QStringLiteral("indicator"), QStringLiteral("motion")};
}

QJsonObject OverlayStyle::fromQuery(const QString &query)
{
    QString q = query.trimmed();
    if (q.startsWith(QLatin1Char('?')))
        q.remove(0, 1);
    q.replace(QLatin1Char('+'), QLatin1Char(' ')); // like the page's URLSearchParams
    const QUrlQuery url(q);
    const auto has = [&](const char *key) { return url.hasQueryItem(QString::fromLatin1(key)); };
    // Empty values count as "not given", like the old page (flags excepted).
    const auto text = [&](const char *key) {
        return url.queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded).trimmed();
    };
    const auto choice = [&](const char *key, const QStringList &allowed) {
        const QString v = text(key).toLower();
        return allowed.contains(v) ? v : QString();
    };

    QJsonObject s;
    const QString style = choice("style", presetNames() << QStringLiteral("plain"));
    const QString position = choice("position", {QStringLiteral("bottom"), QStringLiteral("top"), QStringLiteral("middle")});
    const QString align = choice("align", {QStringLiteral("center"), QStringLiteral("left"), QStringLiteral("right")});
    const QString tail = choice("tail", {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"),
                                         QStringLiteral("none")});

    if (style == QLatin1String("plain")) {
        // The old outlined text without a box, at its old size.
        assign(s, QStringLiteral("preset"), QStringLiteral("outline"));
        assign(s, QStringLiteral("font.uppercase"), false);
        assign(s, QStringLiteral("font.letterSpacing"), 0);
        assign(s, QStringLiteral("font.weight"), 700);
        assign(s, QStringLiteral("font.size"), 42);
        assign(s, QStringLiteral("effects.outline"), 3);
        assign(s, QStringLiteral("box.maxWidth"), 80);
    } else if (!style.isEmpty()) {
        assign(s, QStringLiteral("preset"), style);
    }

    if (!position.isEmpty() || !align.isEmpty() || style == QLatin1String("bubble")) {
        const QString v = position.isEmpty() ? QStringLiteral("bottom") : position;
        const QString h = align.isEmpty() ? QStringLiteral("center") : align;
        QString anchor;
        if (v == QLatin1String("middle"))
            anchor = h == QLatin1String("center") ? QStringLiteral("center") : h;
        else
            anchor = h == QLatin1String("center") ? v : v + QLatin1Char('-') + h;
        assign(s, QStringLiteral("position.anchor"), anchor);
        if (style == QLatin1String("bubble")) { // the old bubble sat where align put it
            assign(s, QStringLiteral("position.offsetX"), 0);
            assign(s, QStringLiteral("position.offsetY"), 0);
        }
    }
    if (!align.isEmpty())
        assign(s, QStringLiteral("box.align"), align);
    if (!tail.isEmpty())
        assign(s, QStringLiteral("box.tail"), tail);
    else if (style == QLatin1String("bubble"))
        assign(s, QStringLiteral("box.tail"), align.isEmpty() ? QStringLiteral("center") : align);

    if (!text("font").isEmpty())
        assign(s, QStringLiteral("font.family"), text("font"));
    const auto number = [&](const char *key, const char *path) {
        const std::optional<double> v = leadingNumber(text(key));
        if (v)
            assign(s, QString::fromLatin1(path), *v);
        return v;
    };
    number("size", "font.size");
    if (const std::optional<LegacyColor> c = legacyColor(text("color")); c && c->opacity > 0)
        assign(s, QStringLiteral("colors.text"), c->hex);
    if (const std::optional<LegacyColor> c = legacyColor(text("bg"))) {
        if (c->opacity > 0)
            assign(s, QStringLiteral("colors.background"), c->hex);
        assign(s, QStringLiteral("colors.backgroundOpacity"), c->opacity);
    }
    if (const std::optional<LegacyColor> c = legacyColor(text("ink")); c && c->opacity > 0)
        assign(s, QStringLiteral("colors.ink"), c->hex);
    number("outline", "effects.outline");
    if (const std::optional<LegacyColor> c = legacyColor(text("outlinecolor")); c && c->opacity > 0)
        assign(s, QStringLiteral("colors.outline"), c->hex);

    const QString reveal = choice("reveal", {QStringLiteral("word"), QStringLiteral("instant")});
    if (!reveal.isEmpty())
        assign(s, QStringLiteral("timing.reveal"),
               reveal == QLatin1String("word") ? QStringLiteral("audio") : QStringLiteral("instant"));
    number("wps", "timing.wps");
    if (const std::optional<double> hold = leadingNumber(text("hold")))
        assign(s, QStringLiteral("timing.hold"), *hold > 60 ? -1.0 : *hold); // long holds meant "keep it"
    number("lines", "history.maxLines"); // old "lines" counted text lines
    number("width", "box.maxWidth");
    if (has("roll")) {
        const bool roll = legacyFlag(url.queryItemValue(QStringLiteral("roll"), QUrl::FullyDecoded));
        assign(s, QStringLiteral("history.roll"), roll);
        assign(s, QStringLiteral("history.lines"), roll ? 2 : 1); // the previous message stayed above
    }
    if (has("name"))
        assign(s, QStringLiteral("name.show"), legacyFlag(url.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded)));
    if (has("indicator"))
        assign(s, QStringLiteral("indicator.show"),
               legacyFlag(url.queryItemValue(QStringLiteral("indicator"), QUrl::FullyDecoded)));
    if (has("motion") && !legacyFlag(url.queryItemValue(QStringLiteral("motion"), QUrl::FullyDecoded))) {
        assign(s, QStringLiteral("animation.enter"), QStringLiteral("fade"));
        assign(s, QStringLiteral("animation.word"), QStringLiteral("fade"));
        assign(s, QStringLiteral("animation.exit"), QStringLiteral("fade"));
    }
    return validatePartial(s);
}

bool OverlayStyle::isProfileId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9-]{1,32}$"));
    return re.match(id).hasMatch();
}

QList<OverlayProfile> OverlayStyle::parseProfiles(const QString &json)
{
    QList<OverlayProfile> out;
    QSet<QString> seen;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        OverlayProfile p;
        p.id = o.value(QStringLiteral("id")).toString();
        if (!isProfileId(p.id) || seen.contains(p.id))
            continue;
        seen.insert(p.id);
        p.name = withoutControls(o.value(QStringLiteral("name")).toString()).trimmed().left(60);
        if (p.name.isEmpty())
            p.name = p.id;
        p.kind = o.value(QStringLiteral("kind")).toString();
        if (!kinds().contains(p.kind))
            p.kind = QStringLiteral("captions");
        p.style = o.value(QStringLiteral("style")).toObject();
        if (p.id == QLatin1String("main"))
            out.prepend(p);
        else
            out.append(p);
    }
    return out;
}

QString OverlayStyle::serializeProfiles(const QList<OverlayProfile> &profiles)
{
    QJsonArray array;
    for (const OverlayProfile &p : profiles) {
        array.append(QJsonObject{{QStringLiteral("id"), p.id},
                                 {QStringLiteral("name"), p.name},
                                 {QStringLiteral("kind"), p.kind},
                                 {QStringLiteral("style"), p.style}});
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<OverlayProfile> OverlayStyle::loadProfiles(Settings *settings)
{
    QList<OverlayProfile> profiles = parseProfiles(settings->string(Keys::OverlayProfiles));
    if (!profiles.isEmpty() && profiles.first().id == QLatin1String("main"))
        return profiles;

    // First start with profiles: the old URL options become the main overlay,
    // and OBS sources that still carry them follow it (Keys::OverlayLegacyQuery).
    const QString legacy = settings->string(Keys::OverlayQuery);
    profiles.prepend({QStringLiteral("main"), tr("Captions"), QStringLiteral("captions"), fromQuery(legacy)});
    saveProfiles(settings, profiles);
    settings->setValue(Keys::OverlayLegacyQuery, legacy);
    return profiles;
}

void OverlayStyle::saveProfiles(Settings *settings, const QList<OverlayProfile> &profiles)
{
    settings->setValue(Keys::OverlayProfiles, serializeProfiles(profiles));
}

QString OverlayStyle::makeId(const QString &name, const QList<OverlayProfile> &existing)
{
    QString base = name.toLower();
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    base.replace(separators, QStringLiteral("-"));
    base = base.left(24);
    while (base.startsWith(QLatin1Char('-')))
        base.remove(0, 1);
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty())
        base = QStringLiteral("overlay");
    QString id = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const OverlayProfile &p : existing)
            taken = taken || p.id == id;
        if (!taken)
            return id;
        id = base + QLatin1Char('-') + QString::number(n);
    }
}

QString OverlayStyle::imageFormat(const QByteArray &data)
{
    QString format;
    QSize size;
    return inspectImage(data, &format, &size) ? format : QString();
}

QString OverlayStyle::importAsset(const QString &sourceFile, const QString &assetDir, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QString();
    };
    if (error)
        error->clear();

    const QFileInfo info(sourceFile);
    if (info.isSymLink())
        return fail(tr("Links and shortcuts can't be used. Pick the image file itself."));
    if (!info.isFile())
        return fail(tr("That file doesn't exist."));
    if (info.size() > kMaxAssetBytes)
        return fail(tr("The image is larger than 10 MB."));

    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly))
        return fail(tr("The image can't be read: %1").arg(file.errorString()));
    const QByteArray data = file.read(kMaxAssetBytes + 1);
    if (data.size() > kMaxAssetBytes)
        return fail(tr("The image is larger than 10 MB."));

    QString format;
    QSize size;
    if (!inspectImage(data, &format, &size))
        return fail(tr("Only PNG, GIF, WebP and JPEG images can be used."));
    if (size.width() > kMaxAssetPixels || size.height() > kMaxAssetPixels)
        return fail(tr("The image is larger than 4096 × 4096 pixels."));

    const QString id =
        QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(16))
        + QLatin1Char('.') + format;
    if (!QDir().mkpath(assetDir))
        return fail(tr("The avatar folder can't be created."));
    const QString target = QDir(assetDir).filePath(id);
    const QFileInfo existing(target);
    if (existing.exists() && existing.isFile() && !existing.isSymLink() && existing.size() == data.size())
        return id; // same content, same name: already imported
    if (existing.exists() || existing.isSymLink())
        QFile::remove(target);

    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        return fail(tr("The image can't be saved: %1").arg(out.errorString()));
    return id;
}

bool OverlayStyle::isAssetId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{16}\\.(png|gif|webp|jpg)$"));
    return re.match(id).hasMatch();
}

QHash<QString, QString> OverlayStyle::scanAssets(const QString &assetDir)
{
    QHash<QString, QString> out;
    if (assetDir.isEmpty())
        return out;
    const QFileInfoList files = QDir(assetDir).entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden);
    for (const QFileInfo &f : files) {
        if (isAssetId(f.fileName()) && !f.isSymLink() && f.size() <= kMaxAssetBytes)
            out.insert(f.fileName(), f.absoluteFilePath());
    }
    return out;
}
