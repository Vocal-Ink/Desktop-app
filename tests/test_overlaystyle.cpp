#include "core/Settings.h"
#include "obs/OverlayStyle.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace {

QJsonValue at(const QJsonObject &o, const QString &path)
{
    QJsonValue v = o;
    for (const QString &key : path.split(QLatin1Char('.')))
        v = v.toObject().value(key);
    return v;
}

// Test JSON is written with single quotes (moc can't read raw strings with quotes inside).
QString J(const char *json)
{
    return QString::fromUtf8(json).replace(QLatin1Char('\''), QLatin1Char('"'));
}

QJsonObject obj(const char *json)
{
    return QJsonDocument::fromJson(J(json).toUtf8()).object();
}

QString effectiveValue(const char *styleJson, const QString &path, const QString &kind = QStringLiteral("captions"))
{
    const QJsonValue v = at(OverlayStyle::effective(obj(styleJson), kind), path);
    return v.isString() ? v.toString() : QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
}

QByteArray pngBytes(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

// 3x2 WebP images (lossless VP8L and lossy VP8), made with Pillow.
const char kWebpLossless[] = "UklGRhwAAABXRUJQVlA4TA8AAAAvAkAAAAcQ/Y/+ByKi/wEA";
const char kWebpLossy[] =
    "UklGRjwAAABXRUJQVlA4IDAAAADQAQCdASoDAAIAAUAmJaACdLoB+AADsAD+8ut//NgVzXPv9//S4P0uD9Lg/9KQAAA=";

} // namespace

class TestOverlayStyle : public QObject
{
    Q_OBJECT
private slots:
    void defaultsMatchTheDocumentation()
    {
        const QJsonObject d = OverlayStyle::defaults(QStringLiteral("captions"));
        const QList<QPair<QString, QJsonValue>> expected = {
            {QStringLiteral("preset"), QStringLiteral("subtitles")},
            {QStringLiteral("font.family"), QStringLiteral("Bricolage Grotesque")},
            {QStringLiteral("font.size"), 44},
            {QStringLiteral("font.weight"), 700},
            {QStringLiteral("font.letterSpacing"), 0},
            {QStringLiteral("font.lineHeight"), 1.25},
            {QStringLiteral("font.uppercase"), false},
            {QStringLiteral("font.italic"), false},
            {QStringLiteral("colors.text"), QStringLiteral("#ffffff")},
            {QStringLiteral("colors.ink"), QStringLiteral("#b48cff")},
            {QStringLiteral("colors.unspoken"), QStringLiteral("#ffffff")},
            {QStringLiteral("colors.unspokenOpacity"), 35},
            {QStringLiteral("colors.background"), QStringLiteral("#120d1f")},
            {QStringLiteral("colors.backgroundOpacity"), 72},
            {QStringLiteral("colors.border"), QStringLiteral("#ffffff")},
            {QStringLiteral("colors.borderOpacity"), 12},
            {QStringLiteral("colors.outline"), QStringLiteral("#000000")},
            {QStringLiteral("colors.shadow"), QStringLiteral("#000000")},
            {QStringLiteral("colors.name"), QStringLiteral("#d9c6ff")},
            {QStringLiteral("box.padding"), 18},
            {QStringLiteral("box.radius"), 14},
            {QStringLiteral("box.borderWidth"), 0},
            {QStringLiteral("box.shadow"), 30},
            {QStringLiteral("box.maxWidth"), 70},
            {QStringLiteral("box.align"), QStringLiteral("center")},
            {QStringLiteral("box.tail"), QStringLiteral("none")},
            {QStringLiteral("effects.outline"), 0},
            {QStringLiteral("effects.textShadow"), 40},
            {QStringLiteral("effects.glow"), 0},
            {QStringLiteral("position.anchor"), QStringLiteral("bottom")},
            {QStringLiteral("position.offsetX"), 0},
            {QStringLiteral("position.offsetY"), 0},
            {QStringLiteral("position.margin"), 48},
            {QStringLiteral("animation.enter"), QStringLiteral("rise")},
            {QStringLiteral("animation.word"), QStringLiteral("ink")},
            {QStringLiteral("animation.exit"), QStringLiteral("fade")},
            {QStringLiteral("animation.speed"), 100},
            {QStringLiteral("timing.reveal"), QStringLiteral("audio")},
            {QStringLiteral("timing.wps"), 2.6},
            {QStringLiteral("timing.hold"), 4},
            {QStringLiteral("history.lines"), 1},
            {QStringLiteral("history.roll"), false},
            {QStringLiteral("history.maxLines"), 3},
            {QStringLiteral("name.show"), false},
            {QStringLiteral("name.position"), QStringLiteral("above")},
            {QStringLiteral("name.text"), QString()},
            {QStringLiteral("indicator.show"), false},
            {QStringLiteral("indicator.style"), QStringLiteral("dot")},
            {QStringLiteral("indicator.position"), QStringLiteral("before")},
            {QStringLiteral("customCss"), QString()},
        };
        for (const auto &e : expected)
            QVERIFY2(at(d, e.first) == e.second, qPrintable(e.first));
        // With no style at all, the page gets exactly the documented defaults.
        QCOMPARE(OverlayStyle::effective({}, QStringLiteral("captions")), d);
        QVERIFY(!d.contains(QStringLiteral("chat")));
        QVERIFY(!d.contains(QStringLiteral("avatar")));

        const QJsonObject chat = OverlayStyle::defaults(QStringLiteral("chat"));
        QCOMPARE(at(chat, QStringLiteral("chat.maxMessages")).toInt(), 5);
        QCOMPARE(at(chat, QStringLiteral("chat.showBadges")).toBool(), true);
        QCOMPARE(at(chat, QStringLiteral("chat.useNameColors")).toBool(), true);
        QCOMPARE(at(chat, QStringLiteral("chat.fadeAfter")).toInt(), 30);
        QCOMPARE(at(chat, QStringLiteral("chat.direction")).toString(), QStringLiteral("up"));
        QCOMPARE(at(chat, QStringLiteral("chat.highlightReading")).toBool(), true);
        QVERIFY(!chat.contains(QStringLiteral("avatar")));

        const QJsonObject avatar = OverlayStyle::defaults(QStringLiteral("avatar"));
        QCOMPARE(at(avatar, QStringLiteral("avatar.images.idle")).toString(), QString());
        QCOMPARE(at(avatar, QStringLiteral("avatar.threshold")).toInt(), 8);
        QCOMPARE(at(avatar, QStringLiteral("avatar.motion")).toString(), QStringLiteral("bounce"));
        QCOMPARE(at(avatar, QStringLiteral("avatar.motionOnlyWhileTalking")).toBool(), true);
        QCOMPARE(at(avatar, QStringLiteral("avatar.intensity")).toInt(), 60);
        QCOMPARE(at(avatar, QStringLiteral("avatar.blinkEvery")).toDouble(), 4.0);
        QCOMPARE(at(avatar, QStringLiteral("avatar.flip")).toBool(), false);
        QCOMPARE(at(avatar, QStringLiteral("avatar.size")).toInt(), 60);
        QCOMPARE(at(avatar, QStringLiteral("avatar.shadow")).toInt(), 0);
        QCOMPARE(at(avatar, QStringLiteral("avatar.micLiveGlow")).toBool(), true);
        QCOMPARE(at(avatar, QStringLiteral("avatar.dimWhenIdle")).toInt(), 0);
        QCOMPARE(at(avatar, QStringLiteral("avatar.anchor")).toString(), QStringLiteral("bottom-left"));
        QVERIFY(!avatar.contains(QStringLiteral("chat")));

        QCOMPARE(OverlayStyle::kinds(), QStringList({QStringLiteral("captions"), QStringLiteral("chat"),
                                                     QStringLiteral("avatar")}));
    }

    void effectiveStyleLayersDefaultsPresetAndStyle()
    {
        // The preset changes the look; what the user set wins; objects merge per field.
        const QJsonObject s = OverlayStyle::effective(
            obj("{'preset':'ink','font':{'size':60},'colors':{'ink':'#ff0000'},'junk':1,'chat':{'maxMessages':3}}"),
            QStringLiteral("captions"));
        QCOMPARE(at(s, QStringLiteral("preset")).toString(), QStringLiteral("ink"));
        QCOMPARE(at(s, QStringLiteral("font.family")).toString(), QStringLiteral("Vocal Ink Display"));
        QCOMPARE(at(s, QStringLiteral("font.size")).toInt(), 60);
        QCOMPARE(at(s, QStringLiteral("colors.ink")).toString(), QStringLiteral("#ff0000"));
        QCOMPARE(at(s, QStringLiteral("colors.backgroundOpacity")).toInt(), 0);
        QCOMPARE(at(s, QStringLiteral("timing.wps")).toDouble(), 2.6); // not part of the preset
        QVERIFY(!s.contains(QStringLiteral("junk")));
        QVERIFY(!s.contains(QStringLiteral("chat"))); // not a chat profile

        // Unknown preset -> subtitles.
        QCOMPARE(effectiveValue("{'preset':'sparkles'}", QStringLiteral("preset")), QStringLiteral("subtitles"));
        QCOMPARE(effectiveValue("{'preset':'NEON'}", QStringLiteral("preset")), QStringLiteral("neon"));

        // Chat and avatar profiles keep their layout under a preset.
        const QJsonObject chat = OverlayStyle::effective(obj("{'preset':'outline'}"), QStringLiteral("chat"));
        QCOMPARE(at(chat, QStringLiteral("font.size")).toInt(), 28);
        QCOMPARE(at(chat, QStringLiteral("font.family")).toString(), QStringLiteral("Vocal Ink Display"));
        QCOMPARE(at(chat, QStringLiteral("position.anchor")).toString(), QStringLiteral("bottom-left"));
        QCOMPARE(at(chat, QStringLiteral("box.align")).toString(), QStringLiteral("left"));
        QCOMPARE(at(chat, QStringLiteral("effects.outline")).toInt(), 7);
        QCOMPARE(at(chat, QStringLiteral("chat.maxMessages")).toInt(), 5);
        const QJsonObject lower = OverlayStyle::presetForKind(QStringLiteral("lowerthird"), QStringLiteral("chat"));
        QVERIFY(!lower.contains(QStringLiteral("position")));
        QVERIFY(!lower.contains(QStringLiteral("name")));
        QVERIFY(at(lower, QStringLiteral("font.size")).isUndefined());
        QCOMPARE(OverlayStyle::presetForKind(QStringLiteral("lowerthird"), QStringLiteral("captions")),
                 OverlayStyle::preset(QStringLiteral("lowerthird")));

        // Unknown kinds are treated as captions.
        QCOMPARE(OverlayStyle::effective({}, QStringLiteral("hologram")), OverlayStyle::defaults(QStringLiteral("captions")));
    }

    void numbersAreClamped()
    {
        QCOMPARE(effectiveValue("{'font':{'size':500}}", QStringLiteral("font.size")), QStringLiteral("[160]"));
        QCOMPARE(effectiveValue("{'font':{'size':-3}}", QStringLiteral("font.size")), QStringLiteral("[12]"));
        QCOMPARE(effectiveValue("{'font':{'size':'48'}}", QStringLiteral("font.size")), QStringLiteral("[48]"));
        QCOMPARE(effectiveValue("{'font':{'size':47.6}}", QStringLiteral("font.size")), QStringLiteral("[48]"));
        QCOMPARE(effectiveValue("{'font':{'size':'big'}}", QStringLiteral("font.size")), QStringLiteral("[44]"));
        QCOMPARE(effectiveValue("{'font':{'size':true}}", QStringLiteral("font.size")), QStringLiteral("[44]"));
        QCOMPARE(effectiveValue("{'font':{'weight':1000}}", QStringLiteral("font.weight")), QStringLiteral("[900]"));
        QCOMPARE(effectiveValue("{'font':{'lineHeight':5}}", QStringLiteral("font.lineHeight")), QStringLiteral("[2]"));
        QCOMPARE(effectiveValue("{'font':{'letterSpacing':-9}}", QStringLiteral("font.letterSpacing")),
                 QStringLiteral("[-5]"));
        QCOMPARE(effectiveValue("{'colors':{'backgroundOpacity':150}}", QStringLiteral("colors.backgroundOpacity")),
                 QStringLiteral("[100]"));
        QCOMPARE(effectiveValue("{'box':{'maxWidth':5}}", QStringLiteral("box.maxWidth")), QStringLiteral("[20]"));
        QCOMPARE(effectiveValue("{'effects':{'outline':40}}", QStringLiteral("effects.outline")), QStringLiteral("[16]"));
        QCOMPARE(effectiveValue("{'position':{'offsetX':-80}}", QStringLiteral("position.offsetX")),
                 QStringLiteral("[-50]"));
        QCOMPARE(effectiveValue("{'position':{'margin':900}}", QStringLiteral("position.margin")), QStringLiteral("[200]"));
        QCOMPARE(effectiveValue("{'animation':{'speed':10}}", QStringLiteral("animation.speed")), QStringLiteral("[25]"));
        QCOMPARE(effectiveValue("{'timing':{'wps':0}}", QStringLiteral("timing.wps")), QStringLiteral("[1]"));
        QCOMPARE(effectiveValue("{'timing':{'wps':20}}", QStringLiteral("timing.wps")), QStringLiteral("[8]"));
        QCOMPARE(effectiveValue("{'timing':{'hold':-5}}", QStringLiteral("timing.hold")), QStringLiteral("[-1]"));
        QCOMPARE(effectiveValue("{'timing':{'hold':-0.5}}", QStringLiteral("timing.hold")), QStringLiteral("[-1]"));
        QCOMPARE(effectiveValue("{'timing':{'hold':600}}", QStringLiteral("timing.hold")), QStringLiteral("[60]"));
        QCOMPARE(effectiveValue("{'timing':{'hold':1.5}}", QStringLiteral("timing.hold")), QStringLiteral("[1.5]"));
        QCOMPARE(effectiveValue("{'history':{'lines':9}}", QStringLiteral("history.lines")), QStringLiteral("[6]"));
        QCOMPARE(effectiveValue("{'history':{'maxLines':0}}", QStringLiteral("history.maxLines")), QStringLiteral("[1]"));
        QCOMPARE(effectiveValue("{'chat':{'maxMessages':50}}", QStringLiteral("chat.maxMessages"), QStringLiteral("chat")),
                 QStringLiteral("[20]"));
        QCOMPARE(effectiveValue("{'chat':{'fadeAfter':-1}}", QStringLiteral("chat.fadeAfter"), QStringLiteral("chat")),
                 QStringLiteral("[0]"));
        QCOMPARE(effectiveValue("{'avatar':{'size':1}}", QStringLiteral("avatar.size"), QStringLiteral("avatar")),
                 QStringLiteral("[10]"));
        QCOMPARE(effectiveValue("{'avatar':{'threshold':99}}", QStringLiteral("avatar.threshold"), QStringLiteral("avatar")),
                 QStringLiteral("[50]"));
        QCOMPARE(effectiveValue("{'avatar':{'blinkEvery':30}}", QStringLiteral("avatar.blinkEvery"),
                                QStringLiteral("avatar")),
                 QStringLiteral("[20]"));
    }

    void coloursAreValidated()
    {
        QVERIFY(OverlayStyle::isValidColor(QStringLiteral("#abc")));
        QVERIFY(OverlayStyle::isValidColor(QStringLiteral("#A0B1C2")));
        QVERIFY(!OverlayStyle::isValidColor(QStringLiteral("#aabbccdd"))); // alpha is a separate field
        QVERIFY(!OverlayStyle::isValidColor(QStringLiteral("abc")));
        QVERIFY(!OverlayStyle::isValidColor(QStringLiteral("red")));
        QVERIFY(!OverlayStyle::isValidColor(QStringLiteral("#ggg")));

        QCOMPARE(effectiveValue("{'colors':{'text':'#ABC'}}", QStringLiteral("colors.text")), QStringLiteral("#aabbcc"));
        QCOMPARE(effectiveValue("{'colors':{'text':'#FFCC00'}}", QStringLiteral("colors.text")), QStringLiteral("#ffcc00"));
        for (const char *bad : {"{'colors':{'text':'#aabbccdd'}}", "{'colors':{'text':'red'}}",
                                "{'colors':{'text':'rgb(1,2,3)'}}", "{'colors':{'text':42}}",
                                "{'colors':{'text':'#fff;background:url(x)'}}"})
            QCOMPARE(effectiveValue(bad, QStringLiteral("colors.text")), QStringLiteral("#ffffff"));
        // A bad colour falls back to the preset's value, not the plain default.
        QCOMPARE(effectiveValue("{'preset':'bubble','colors':{'text':'nope'}}", QStringLiteral("colors.text")),
                 QStringLiteral("#1d1830"));
    }

    void enumsBoolsAndStrings()
    {
        QCOMPARE(effectiveValue("{'box':{'align':'LEFT'}}", QStringLiteral("box.align")), QStringLiteral("left"));
        QCOMPARE(effectiveValue("{'box':{'align':'diagonal'}}", QStringLiteral("box.align")), QStringLiteral("center"));
        QCOMPARE(effectiveValue("{'position':{'anchor':'top-right'}}", QStringLiteral("position.anchor")),
                 QStringLiteral("top-right"));
        QCOMPARE(effectiveValue("{'animation':{'word':'explode'}}", QStringLiteral("animation.word")),
                 QStringLiteral("ink"));
        QCOMPARE(effectiveValue("{'timing':{'reveal':3}}", QStringLiteral("timing.reveal")), QStringLiteral("audio"));

        QCOMPARE(effectiveValue("{'name':{'show':'yes'}}", QStringLiteral("name.show")), QStringLiteral("[true]"));
        QCOMPARE(effectiveValue("{'name':{'show':1}}", QStringLiteral("name.show")), QStringLiteral("[true]"));
        QCOMPARE(effectiveValue("{'name':{'show':'off'}}", QStringLiteral("name.show")), QStringLiteral("[false]"));
        QCOMPARE(effectiveValue("{'name':{'show':'maybe'}}", QStringLiteral("name.show")), QStringLiteral("[false]"));
        QCOMPARE(effectiveValue("{'preset':'lowerthird','name':{'show':'maybe'}}", QStringLiteral("name.show")),
                 QStringLiteral("[true]"));

        const QString longName(200, QLatin1Char('x'));
        const QJsonObject s = OverlayStyle::effective(
            QJsonObject{{QStringLiteral("name"), QJsonObject{{QStringLiteral("text"), longName + QStringLiteral("\n\t")}}}},
            QStringLiteral("captions"));
        QCOMPARE(at(s, QStringLiteral("name.text")).toString(), QString(60, QLatin1Char('x')));

        // Font names go into CSS: nothing that could end the declaration survives.
        const auto family = [](const QString &value) {
            const QJsonObject style{{QStringLiteral("font"), QJsonObject{{QStringLiteral("family"), value}}}};
            return at(OverlayStyle::effective(style, QStringLiteral("captions")), QStringLiteral("font.family")).toString();
        };
        QCOMPARE(family(QStringLiteral("Comic Sans MS\"; } body { color: red")),
                 QStringLiteral("Comic Sans MS body color red"));
        QCOMPARE(effectiveValue("{'font':{'family':'Inter, Arial'}}", QStringLiteral("font.family")),
                 QStringLiteral("Inter, Arial"));
        QCOMPARE(family(QStringLiteral("\"\";")),
                 QStringLiteral("Bricolage Grotesque"));
    }

    void avatarImagesMustBeAssetIds()
    {
        const auto image = [](const char *value) {
            const QJsonObject style{{QStringLiteral("avatar"),
                                     QJsonObject{{QStringLiteral("images"),
                                                  QJsonObject{{QStringLiteral("idle"), QString::fromUtf8(value)}}}}}};
            return at(OverlayStyle::effective(style, QStringLiteral("avatar")), QStringLiteral("avatar.images.idle")).toString();
        };
        QCOMPARE(image("0123456789abcdef.png"), QStringLiteral("0123456789abcdef.png"));
        QCOMPARE(image("0123456789abcdef.webp"), QStringLiteral("0123456789abcdef.webp"));
        QCOMPARE(image("0123456789ABCDEF.png"), QString());
        QCOMPARE(image("0123456789abcdef.svg"), QString());
        QCOMPARE(image("../../etc/passwd"), QString());
        QCOMPARE(image("http://example.com/a.png"), QString());

        QVERIFY(OverlayStyle::isAssetId(QStringLiteral("0123456789abcdef.jpg")));
        QVERIFY(!OverlayStyle::isAssetId(QStringLiteral("0123456789abcdef.jpeg")));
        QVERIFY(!OverlayStyle::isAssetId(QStringLiteral("0123456789abcde.png")));
        QVERIFY(!OverlayStyle::isAssetId(QStringLiteral("0123456789abcdef.png/x")));
    }

    void customCssIsSanitized()
    {
        using OverlayStyle::sanitizeCss;
        const QString plain = QStringLiteral(".vi-word.spoken { color: #ff0; transform: scale(1.1); }\n#vi-root > .vi-box { gap: 4px }");
        QCOMPARE(sanitizeCss(plain), plain);

        const QStringList attacks = {
            QStringLiteral("@import url(https://evil.example/x.css); .a{}"),
            QStringLiteral("@IMPORT 'https://evil.example/x.css'; .a{}"),
            QStringLiteral("@\\69mport 'https://evil.example/x.css'; .a{}"),
            QStringLiteral("@im/**/port 'x.css';"),
            QStringLiteral(".a { background: url(https://evil.example/track.png) }"),
            QStringLiteral(".a { background: URL( \"//evil.example/x\" ) }"),
            QStringLiteral(".a { background: u\\rl(https://evil.example/x) }"),
            QStringLiteral(".a { background: \\75 rl(https://evil.example/x) }"),
            QStringLiteral(".a { background: url(/assets/../secret) }"),
            QStringLiteral(".a { background: url(data:image/png;base64,AAAA) }"),
            QStringLiteral(".a { background: -webkit-image-set('https://evil.example/x.png' 1x) }"),
            QStringLiteral(".a { width: expression(alert(1)) }"),
            QStringLiteral(".a { behavior: url(x.htc) }"),
            QStringLiteral(".a { -moz-binding: url(x.xml#x) }"),
            QStringLiteral(".a { background: javascript:alert(1) }"),
            QStringLiteral("</style><script>alert(1)</script>"),
        };
        for (const QString &css : attacks) {
            const QString out = sanitizeCss(css).toLower();
            QVERIFY2(!out.contains(QLatin1String("import")), qPrintable(css + QStringLiteral(" -> ") + out));
            QVERIFY2(!out.contains(QLatin1String("evil")) || !out.contains(QLatin1String("url(")),
                     qPrintable(css + QStringLiteral(" -> ") + out));
            QVERIFY2(!out.contains(QLatin1String("url(http")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("url(\"//")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("url(data")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("..")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("expression")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("behavior")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("-moz-binding")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("javascript:")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("image-set")), qPrintable(out));
            QVERIFY2(!out.contains(QLatin1String("</")), qPrintable(out));
        }

        // The page's own fonts and images stay usable.
        QCOMPARE(sanitizeCss(QStringLiteral(".a { background: url(/assets/0123456789abcdef.png) }")),
                 QStringLiteral(".a { background: url(\"/assets/0123456789abcdef.png\") }"));
        QCOMPARE(sanitizeCss(QStringLiteral("@font-face { src: url('/fonts/Lexend-Regular.ttf') }")),
                 QStringLiteral("@font-face { src: url(\"/fonts/Lexend-Regular.ttf\") }"));
        // Length cap.
        QCOMPARE(sanitizeCss(QString(20000, QLatin1Char('a'))).size(), OverlayStyle::kMaxCustomCss);
        // And effective() runs it.
        QCOMPARE(at(OverlayStyle::effective(QJsonObject{{QStringLiteral("customCss"), QStringLiteral("@import 'x.css'; .vi-word { color: red }")}},
                                   QStringLiteral("captions")),
                    QStringLiteral("customCss"))
                     .toString(),
                 QStringLiteral(".vi-word { color: red }"));
    }

    void presetsAreDistinct()
    {
        const QStringList names = OverlayStyle::presetNames();
        QCOMPARE(names, QStringList({QStringLiteral("subtitles"), QStringLiteral("ink"), QStringLiteral("bubble"),
                                     QStringLiteral("outline"), QStringLiteral("karaoke"), QStringLiteral("typewriter"),
                                     QStringLiteral("neon"), QStringLiteral("lowerthird"), QStringLiteral("minimal")}));
        QSet<QByteArray> seen;
        for (const QString &name : names) {
            const QJsonObject p = OverlayStyle::preset(name);
            QVERIFY2(!p.isEmpty(), qPrintable(name));
            // Every preset sets the whole look, and only valid values.
            for (const char *path : {"font.family", "font.size", "colors.text", "colors.ink", "colors.background",
                                     "colors.backgroundOpacity", "box.padding", "box.radius", "box.maxWidth",
                                     "effects.outline", "effects.textShadow", "effects.glow", "animation.enter",
                                     "animation.word", "animation.exit"})
                QVERIFY2(!at(p, QString::fromLatin1(path)).isUndefined(), qPrintable(name + QLatin1Char(' ') + QLatin1String(path)));
            QJsonObject withPreset = p;
            withPreset.insert(QStringLiteral("preset"), name);
            const QJsonObject eff = OverlayStyle::effective(withPreset, QStringLiteral("captions"));
            QCOMPARE(OverlayStyle::merge(eff, p), eff); // nothing in the preset was out of range
            const QByteArray look = QJsonDocument(eff).toJson(QJsonDocument::Compact);
            QVERIFY2(!seen.contains(look), qPrintable(name));
            seen.insert(look);
        }
        QVERIFY(OverlayStyle::preset(QStringLiteral("nope")).isEmpty());

        // A few signature traits.
        QCOMPARE(at(OverlayStyle::preset(QStringLiteral("bubble")), QStringLiteral("box.tail")).toString(), QStringLiteral("left"));
        QCOMPARE(at(OverlayStyle::preset(QStringLiteral("typewriter")), QStringLiteral("animation.word")).toString(),
                 QStringLiteral("type"));
        QVERIFY(at(OverlayStyle::preset(QStringLiteral("neon")), QStringLiteral("effects.glow")).toInt() > 50);
        QVERIFY(at(OverlayStyle::preset(QStringLiteral("outline")), QStringLiteral("effects.outline")).toInt() >= 5);
        QCOMPARE(at(OverlayStyle::preset(QStringLiteral("lowerthird")), QStringLiteral("position.anchor")).toString(),
                 QStringLiteral("bottom-left"));
        QCOMPARE(at(OverlayStyle::preset(QStringLiteral("karaoke")), QStringLiteral("colors.unspokenOpacity")).toInt(), 100);
    }

    void legacyQueryMapsEveryOption()
    {
        using OverlayStyle::fromQuery;
        const auto v = [](const QString &query, const char *path) { return at(fromQuery(query), QString::fromLatin1(path)); };

        QVERIFY(fromQuery(QString()).isEmpty());
        QVERIFY(fromQuery(QStringLiteral("demo=1&profile=chat&unknown=3")).isEmpty());
        QCOMPARE(fromQuery(QStringLiteral("style=subtitles")), obj("{'preset':'subtitles'}"));
        QCOMPARE(v(QStringLiteral("?style=ink"), "preset").toString(), QStringLiteral("ink"));
        QCOMPARE(v(QStringLiteral("style=neon"), "preset").toString(), QStringLiteral("neon"));
        QVERIFY(v(QStringLiteral("style=sparkles"), "preset").isUndefined());

        // plain: the old outlined text without a box.
        const QJsonObject plain = fromQuery(QStringLiteral("style=plain"));
        QCOMPARE(at(plain, QStringLiteral("preset")).toString(), QStringLiteral("outline"));
        QCOMPARE(at(plain, QStringLiteral("font.size")).toInt(), 42);
        QCOMPARE(at(plain, QStringLiteral("font.uppercase")).toBool(), false);
        QCOMPARE(at(plain, QStringLiteral("effects.outline")).toInt(), 3);
        QCOMPARE(at(plain, QStringLiteral("box.maxWidth")).toInt(), 80);
        QCOMPARE(v(QStringLiteral("style=plain&size=60&outline=5&width=50"), "font.size").toInt(), 60);
        QCOMPARE(v(QStringLiteral("style=plain&size=60&outline=5&width=50"), "effects.outline").toInt(), 5);
        QCOMPARE(v(QStringLiteral("style=plain&size=60&outline=5&width=50"), "box.maxWidth").toInt(), 50);

        // position + align -> anchor (and the text alignment).
        QCOMPARE(v(QStringLiteral("position=top"), "position.anchor").toString(), QStringLiteral("top"));
        QCOMPARE(v(QStringLiteral("position=top&align=left"), "position.anchor").toString(), QStringLiteral("top-left"));
        QCOMPARE(v(QStringLiteral("position=middle"), "position.anchor").toString(), QStringLiteral("center"));
        QCOMPARE(v(QStringLiteral("position=middle&align=right"), "position.anchor").toString(), QStringLiteral("right"));
        QCOMPARE(v(QStringLiteral("align=right"), "position.anchor").toString(), QStringLiteral("bottom-right"));
        QCOMPARE(v(QStringLiteral("align=right"), "box.align").toString(), QStringLiteral("right"));
        QVERIFY(v(QStringLiteral("position=top"), "box.align").isUndefined());
        QVERIFY(v(QStringLiteral("position=sideways"), "position.anchor").isUndefined()); // the old page ignored it too

        // The old bubble sat where align put it, with the tail on the same side.
        const QJsonObject bubble = fromQuery(QStringLiteral("style=bubble&align=left"));
        QCOMPARE(at(bubble, QStringLiteral("position.anchor")).toString(), QStringLiteral("bottom-left"));
        QCOMPARE(at(bubble, QStringLiteral("box.tail")).toString(), QStringLiteral("left"));
        QCOMPARE(at(bubble, QStringLiteral("position.offsetX")).toInt(), 0);
        QCOMPARE(v(QStringLiteral("style=bubble"), "box.tail").toString(), QStringLiteral("center"));
        QCOMPARE(v(QStringLiteral("style=bubble&tail=none"), "box.tail").toString(), QStringLiteral("none"));
        QCOMPARE(v(QStringLiteral("tail=right"), "box.tail").toString(), QStringLiteral("right"));

        QCOMPARE(v(QStringLiteral("font=Comic%20Sans%20MS"), "font.family").toString(), QStringLiteral("Comic Sans MS"));
        QCOMPARE(v(QStringLiteral("font=Comic+Sans+MS"), "font.family").toString(), QStringLiteral("Comic Sans MS"));
        QCOMPARE(v(QStringLiteral("size=48"), "font.size").toInt(), 48);
        QCOMPARE(v(QStringLiteral("size=48px"), "font.size").toInt(), 48);
        QCOMPARE(v(QStringLiteral("size=300"), "font.size").toInt(), 160);
        QVERIFY(v(QStringLiteral("size=huge"), "font.size").isUndefined());

        QCOMPARE(v(QStringLiteral("color=ffcc00"), "colors.text").toString(), QStringLiteral("#ffcc00"));
        QCOMPARE(v(QStringLiteral("color=%23FFCC00"), "colors.text").toString(), QStringLiteral("#ffcc00"));
        QCOMPARE(v(QStringLiteral("color=fc0"), "colors.text").toString(), QStringLiteral("#ffcc00"));
        QCOMPARE(v(QStringLiteral("color=gold"), "colors.text").toString(), QStringLiteral("#ffd700"));
        QVERIFY(v(QStringLiteral("color=notacolour"), "colors.text").isUndefined());

        QCOMPARE(v(QStringLiteral("bg=000000aa"), "colors.background").toString(), QStringLiteral("#000000"));
        QCOMPARE(v(QStringLiteral("bg=000000aa"), "colors.backgroundOpacity").toInt(), 67);
        QCOMPARE(v(QStringLiteral("bg=rgba(0,0,0,0.85)"), "colors.backgroundOpacity").toInt(), 85);
        QCOMPARE(v(QStringLiteral("bg=rgba(10,20,30,0.85)"), "colors.background").toString(), QStringLiteral("#0a141e"));
        QCOMPARE(v(QStringLiteral("bg=rgb(255%2C255%2C255)"), "colors.background").toString(), QStringLiteral("#ffffff"));
        QCOMPARE(v(QStringLiteral("bg=rgb(255%2C255%2C255)"), "colors.backgroundOpacity").toInt(), 100);
        QCOMPARE(v(QStringLiteral("bg=none"), "colors.backgroundOpacity").toInt(), 0);
        QVERIFY(v(QStringLiteral("bg=none"), "colors.background").isUndefined());
        QCOMPARE(v(QStringLiteral("bg=transparent"), "colors.backgroundOpacity").toInt(), 0);

        QCOMPARE(v(QStringLiteral("ink=b18cff"), "colors.ink").toString(), QStringLiteral("#b18cff"));
        QCOMPARE(v(QStringLiteral("outline=4"), "effects.outline").toInt(), 4);
        QCOMPARE(v(QStringLiteral("outline=24"), "effects.outline").toInt(), 16);
        QCOMPARE(v(QStringLiteral("outlinecolor=ff0000"), "colors.outline").toString(), QStringLiteral("#ff0000"));

        QCOMPARE(v(QStringLiteral("reveal=word"), "timing.reveal").toString(), QStringLiteral("audio"));
        QCOMPARE(v(QStringLiteral("reveal=instant"), "timing.reveal").toString(), QStringLiteral("instant"));
        QVERIFY(v(QStringLiteral("reveal=later"), "timing.reveal").isUndefined());
        QCOMPARE(v(QStringLiteral("wps=3.5"), "timing.wps").toDouble(), 3.5);
        QCOMPARE(v(QStringLiteral("wps=30"), "timing.wps").toDouble(), 8.0);
        QCOMPARE(v(QStringLiteral("hold=8"), "timing.hold").toDouble(), 8.0);
        QCOMPARE(v(QStringLiteral("hold=-1"), "timing.hold").toDouble(), -1.0);
        QCOMPARE(v(QStringLiteral("hold=86400"), "timing.hold").toDouble(), -1.0);
        QCOMPARE(v(QStringLiteral("lines=3"), "history.maxLines").toInt(), 3);
        QCOMPARE(v(QStringLiteral("width=42"), "box.maxWidth").toInt(), 42);
        QCOMPARE(v(QStringLiteral("roll=1"), "history.roll").toBool(), true);
        QCOMPARE(v(QStringLiteral("roll=1"), "history.lines").toInt(), 2);
        QCOMPARE(v(QStringLiteral("roll=0"), "history.roll").toBool(), false);
        QCOMPARE(v(QStringLiteral("roll=0"), "history.lines").toInt(), 1);
        QCOMPARE(v(QStringLiteral("name=1"), "name.show").toBool(), true);
        QCOMPARE(v(QStringLiteral("name"), "name.show").toBool(), true); // a bare flag
        QCOMPARE(v(QStringLiteral("name=no"), "name.show").toBool(), false);
        QCOMPARE(v(QStringLiteral("indicator=true"), "indicator.show").toBool(), true);
        QVERIFY(v(QStringLiteral("motion=1"), "animation").isUndefined());
        QCOMPARE(v(QStringLiteral("motion=0"), "animation.enter").toString(), QStringLiteral("fade"));
        QCOMPARE(v(QStringLiteral("motion=0"), "animation.word").toString(), QStringLiteral("fade"));
        QCOMPARE(v(QStringLiteral("motion=0"), "animation.exit").toString(), QStringLiteral("fade"));

        // Everything together, as an old link from the docs.
        const QJsonObject all = fromQuery(QStringLiteral("style=bubble&align=left&name=1&size=50&hold=8&lines=3"));
        QCOMPARE(at(all, QStringLiteral("preset")).toString(), QStringLiteral("bubble"));
        QCOMPARE(at(all, QStringLiteral("name.show")).toBool(), true);
        QCOMPARE(at(all, QStringLiteral("font.size")).toInt(), 50);
        QCOMPARE(at(all, QStringLiteral("timing.hold")).toDouble(), 8.0);
        QCOMPARE(at(all, QStringLiteral("history.maxLines")).toInt(), 3);
        QCOMPARE(OverlayStyle::queryOptionNames().size(), 20);
    }

    void profilesMigrateFromTheOldQuery()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        settings.setValue(Keys::OverlayQuery, QStringLiteral("style=bubble&align=left&size=50"));

        const QList<OverlayProfile> profiles = OverlayStyle::loadProfiles(&settings);
        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles.first().id, QStringLiteral("main"));
        QCOMPARE(profiles.first().kind, QStringLiteral("captions"));
        QVERIFY(!profiles.first().name.isEmpty());
        QCOMPARE(profiles.first().style, OverlayStyle::fromQuery(QStringLiteral("style=bubble&align=left&size=50")));
        QCOMPARE(at(profiles.first().style, QStringLiteral("preset")).toString(), QStringLiteral("bubble"));
        QCOMPARE(at(profiles.first().style, QStringLiteral("font.size")).toInt(), 50);
        QCOMPARE(at(profiles.first().style, QStringLiteral("position.anchor")).toString(), QStringLiteral("bottom-left"));

        // Saved, remembered as the legacy query; the old key is left alone.
        QCOMPARE(settings.string(Keys::OverlayLegacyQuery), QStringLiteral("style=bubble&align=left&size=50"));
        QCOMPARE(settings.string(Keys::OverlayQuery), QStringLiteral("style=bubble&align=left&size=50"));
        const QList<OverlayProfile> stored = OverlayStyle::parseProfiles(settings.string(Keys::OverlayProfiles));
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().style, profiles.first().style);

        // Later edits stick: no second migration.
        settings.setValue(Keys::OverlayQuery, QStringLiteral("style=ink"));
        QList<OverlayProfile> edited = stored;
        edited.first().style = obj("{'preset':'neon'}");
        OverlayStyle::saveProfiles(&settings, edited);
        const QList<OverlayProfile> again = OverlayStyle::loadProfiles(&settings);
        QCOMPARE(at(again.first().style, QStringLiteral("preset")).toString(), QStringLiteral("neon"));
        QCOMPARE(settings.string(Keys::OverlayLegacyQuery), QStringLiteral("style=bubble&align=left&size=50"));
    }

    void profilesMigrateWithTheDefaultQuery()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        const QList<OverlayProfile> profiles = OverlayStyle::loadProfiles(&settings);
        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles.first().style, obj("{'preset':'subtitles'}"));
        QCOMPARE(settings.string(Keys::OverlayLegacyQuery), QStringLiteral("style=subtitles"));
    }

    void profilesAreCleanedUp()
    {
        const QList<OverlayProfile> p = OverlayStyle::parseProfiles(J("["
            "            {'id':'chat','name':'Chat','kind':'chat','style':{'chat':{'maxMessages':6}}},"
            "            {'id':'Bad Id','name':'x','kind':'captions'},"
            "            {'id':'','kind':'captions'},"
            "            {'id':'this-id-is-far-too-long-to-be-accepted-here','kind':'captions'},"
            "            {'id':'chat','name':'Duplicate','kind':'avatar'},"
            "            {'id':'main','name':'Captions','kind':'captions','style':{'preset':'ink'}},"
            "            {'id':'me','name':'','kind':'hologram','style':'not an object'}"
            "        ]"));
        QCOMPARE(p.size(), 3);
        QCOMPARE(p.at(0).id, QStringLiteral("main"));
        QCOMPARE(p.at(1).id, QStringLiteral("chat"));
        QCOMPARE(p.at(1).name, QStringLiteral("Chat"));
        QCOMPARE(p.at(1).kind, QStringLiteral("chat"));
        QCOMPARE(p.at(2).id, QStringLiteral("me"));
        QCOMPARE(p.at(2).kind, QStringLiteral("captions"));
        QCOMPARE(p.at(2).name, QStringLiteral("me"));
        QVERIFY(p.at(2).style.isEmpty());
        QCOMPARE(OverlayStyle::parseProfiles(OverlayStyle::serializeProfiles(p)).size(), 3);
        QVERIFY(OverlayStyle::parseProfiles(QStringLiteral("garbage")).isEmpty());

        // Profiles without "main" get it back, first, from the old query.
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        settings.setValue(Keys::OverlayQuery, QStringLiteral("style=ink"));
        settings.setValue(Keys::OverlayProfiles,
                          J("[{'id':'chat','name':'Chat','kind':'chat','style':{}}]"));
        const QList<OverlayProfile> loaded = OverlayStyle::loadProfiles(&settings);
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).id, QStringLiteral("main"));
        QCOMPARE(at(loaded.at(0).style, QStringLiteral("preset")).toString(), QStringLiteral("ink"));
        QCOMPARE(loaded.at(1).id, QStringLiteral("chat"));
    }

    void makesUrlSafeIds()
    {
        const QList<OverlayProfile> existing = {{QStringLiteral("main"), {}, {}, {}}, {QStringLiteral("my-chat"), {}, {}, {}}};
        QCOMPARE(OverlayStyle::makeId(QStringLiteral("Gameplay Captions!"), existing), QStringLiteral("gameplay-captions"));
        QCOMPARE(OverlayStyle::makeId(QStringLiteral("My Chat"), existing), QStringLiteral("my-chat-2"));
        QCOMPARE(OverlayStyle::makeId(QStringLiteral("Main"), existing), QStringLiteral("main-2"));
        QCOMPARE(OverlayStyle::makeId(QStringLiteral("✨✨"), existing), QStringLiteral("overlay"));
        const QString longId = OverlayStyle::makeId(QString(100, QLatin1Char('a')), existing);
        QVERIFY(OverlayStyle::isProfileId(longId));
        QVERIFY(OverlayStyle::isProfileId(QStringLiteral("a")));
        QVERIFY(!OverlayStyle::isProfileId(QStringLiteral("A")));
        QVERIFY(!OverlayStyle::isProfileId(QString(33, QLatin1Char('a'))));
    }

    void importsAvatarImages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString assets = dir.filePath(QStringLiteral("avatars"));

        const QString png = dir.filePath(QStringLiteral("idle.png"));
        QVERIFY(writeFile(png, pngBytes(64, 48)));
        QString error;
        const QString id = OverlayStyle::importAsset(png, assets, &error);
        QVERIFY2(OverlayStyle::isAssetId(id), qPrintable(error));
        QVERIFY(id.endsWith(QLatin1String(".png")));
        QVERIFY(QFile::exists(QDir(assets).filePath(id)));
        // Idempotent: same content, same id, still one file.
        QCOMPARE(OverlayStyle::importAsset(png, assets), id);
        QCOMPARE(QDir(assets).entryList(QDir::Files).size(), 1);

        // The real format decides the extension, not the file name.
        const QString disguised = dir.filePath(QStringLiteral("talking.gif"));
        QVERIFY(writeFile(disguised, pngBytes(10, 10)));
        QVERIFY(OverlayStyle::importAsset(disguised, assets).endsWith(QLatin1String(".png")));

        QImage jpegImage(20, 20, QImage::Format_RGB32);
        jpegImage.fill(Qt::blue);
        const QString jpeg = dir.filePath(QStringLiteral("blink.jpeg"));
        if (jpegImage.save(jpeg, "JPEG"))
            QVERIFY(OverlayStyle::importAsset(jpeg, assets).endsWith(QLatin1String(".jpg")));
        const QString gif = dir.filePath(QStringLiteral("anim.gif"));
        QVERIFY(writeFile(gif, QByteArray::fromHex("47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b")));
        QVERIFY(OverlayStyle::importAsset(gif, assets).endsWith(QLatin1String(".gif")));
        for (const char *webp : {kWebpLossless, kWebpLossy}) {
            const QString path = dir.filePath(QStringLiteral("a.webp"));
            QFile::remove(path);
            QVERIFY(writeFile(path, QByteArray::fromBase64(webp)));
            QCOMPARE(OverlayStyle::imageFormat(QByteArray::fromBase64(webp)), QStringLiteral("webp"));
            const QString webpId = OverlayStyle::importAsset(path, assets, &error);
            QVERIFY2(webpId.endsWith(QLatin1String(".webp")), qPrintable(error));
        }

        // Rejected: SVG, other formats, text, too big, too many pixels, missing, links.
        const QString svg = dir.filePath(QStringLiteral("evil.png"));
        QVERIFY(writeFile(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>"));
        QVERIFY(OverlayStyle::importAsset(svg, assets, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(OverlayStyle::imageFormat(QByteArray("<svg></svg>")), QString());
        QCOMPARE(OverlayStyle::imageFormat(QByteArray("BM\x36\x00\x00\x00")), QString());

        const QString huge = dir.filePath(QStringLiteral("huge.png"));
        QByteArray big = pngBytes(8, 8);
        big.append(QByteArray(int(OverlayStyle::kMaxAssetBytes), '\0'));
        QVERIFY(writeFile(huge, big));
        QVERIFY(OverlayStyle::importAsset(huge, assets, &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("10 MB")));

        const QString wide = dir.filePath(QStringLiteral("wide.png"));
        QVERIFY(writeFile(wide, pngBytes(4097, 2)));
        QVERIFY(OverlayStyle::importAsset(wide, assets, &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("4096")));
        const QString edge = dir.filePath(QStringLiteral("edge.png"));
        QVERIFY(writeFile(edge, pngBytes(4096, 2)));
        QVERIFY(!OverlayStyle::importAsset(edge, assets).isEmpty());

        QVERIFY(OverlayStyle::importAsset(dir.filePath(QStringLiteral("missing.png")), assets, &error).isEmpty());

#ifndef Q_OS_WIN
        const QString link = dir.filePath(QStringLiteral("link.png"));
        QVERIFY(QFile::link(png, link));
        QVERIFY(OverlayStyle::importAsset(link, assets, &error).isEmpty());
        QVERIFY(!error.isEmpty());
#endif

        // Only proper asset files are listed.
        QVERIFY(writeFile(QDir(assets).filePath(QStringLiteral("notes.txt")), "x"));
        QVERIFY(writeFile(QDir(assets).filePath(QStringLiteral("0123456789ABCDEF.png")), pngBytes(2, 2)));
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(png, QDir(assets).filePath(QStringLiteral("aaaaaaaaaaaaaaaa.png"))));
#endif
        const QHash<QString, QString> scanned = OverlayStyle::scanAssets(assets);
        QVERIFY(scanned.contains(id));
        QCOMPARE(QFileInfo(scanned.value(id)).absoluteFilePath(), QFileInfo(QDir(assets).filePath(id)).absoluteFilePath());
        QVERIFY(!scanned.contains(QStringLiteral("notes.txt")));
        QVERIFY(!scanned.contains(QStringLiteral("0123456789ABCDEF.png")));
        QVERIFY(!scanned.contains(QStringLiteral("aaaaaaaaaaaaaaaa.png")));
        for (auto it = scanned.begin(); it != scanned.end(); ++it)
            QVERIFY(OverlayStyle::isAssetId(it.key()));
        QVERIFY(OverlayStyle::scanAssets(dir.filePath(QStringLiteral("nothing-here"))).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestOverlayStyle)
#include "test_overlaystyle.moc"
