#include "obs/OverlayStyle.h"

#include "core/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

// Placeholder implementation of the contract; the full style model (presets,
// clamping, legacy query mapping) lands with the overlay engine.

QStringList OverlayStyle::kinds()
{
    return {QStringLiteral("captions"), QStringLiteral("chat"), QStringLiteral("avatar")};
}

QStringList OverlayStyle::presetNames()
{
    return {QStringLiteral("subtitles"), QStringLiteral("ink"),        QStringLiteral("bubble"),
            QStringLiteral("outline"),   QStringLiteral("karaoke"),    QStringLiteral("typewriter"),
            QStringLiteral("neon"),      QStringLiteral("lowerthird"), QStringLiteral("minimal")};
}

QJsonObject OverlayStyle::defaults(const QString &kind)
{
    Q_UNUSED(kind)
    return {{QStringLiteral("preset"), QStringLiteral("subtitles")}};
}

QJsonObject OverlayStyle::preset(const QString &name)
{
    Q_UNUSED(name)
    return {};
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
    return merge(merge(defaults(kind), preset(style.value(QStringLiteral("preset")).toString())), style);
}

bool OverlayStyle::isValidColor(const QString &color)
{
    static const QRegularExpression re(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    return re.match(color).hasMatch();
}

QString OverlayStyle::sanitizeCss(const QString &css)
{
    Q_UNUSED(css)
    return QString();
}

QJsonObject OverlayStyle::fromQuery(const QString &query)
{
    Q_UNUSED(query)
    return {};
}

QList<OverlayProfile> OverlayStyle::parseProfiles(const QString &json)
{
    QList<OverlayProfile> out;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        OverlayProfile p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString();
        p.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("captions"));
        p.style = o.value(QStringLiteral("style")).toObject();
        if (!p.id.isEmpty())
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
    bool hasMain = false;
    for (const OverlayProfile &p : profiles)
        hasMain = hasMain || p.id == QLatin1String("main");
    if (!hasMain)
        profiles.prepend({QStringLiteral("main"), QStringLiteral("Captions"), QStringLiteral("captions"),
                          fromQuery(settings->string(Keys::OverlayQuery))});
    return profiles;
}

void OverlayStyle::saveProfiles(Settings *settings, const QList<OverlayProfile> &profiles)
{
    settings->setValue(Keys::OverlayProfiles, serializeProfiles(profiles));
}

QString OverlayStyle::makeId(const QString &name, const QList<OverlayProfile> &existing)
{
    QString base = name.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
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

QString OverlayStyle::importAsset(const QString &sourceFile, const QString &assetDir, QString *error)
{
    Q_UNUSED(sourceFile)
    Q_UNUSED(assetDir)
    if (error)
        *error = QString();
    return QString();
}

bool OverlayStyle::isAssetId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{16}\\.(png|gif|webp|jpg)$"));
    return re.match(id).hasMatch();
}

QHash<QString, QString> OverlayStyle::scanAssets(const QString &assetDir)
{
    Q_UNUSED(assetDir)
    return {};
}
