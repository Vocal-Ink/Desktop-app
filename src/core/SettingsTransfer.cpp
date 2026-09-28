#include "core/SettingsTransfer.h"

#include "core/Paths.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr int kFormatVersion = 1;
constexpr auto kBytesTag = "@bytes"; // QByteArray values (e.g. audio device ids) as base64

QString appName()
{
    return QStringLiteral("Vocal Ink");
}

QString writeError(const QString &path, const QString &reason)
{
    return QCoreApplication::translate("SettingsTransfer", "Could not write %1: %2")
        .arg(QDir::toNativeSeparators(path), reason);
}

QString damagedError()
{
    return QCoreApplication::translate("SettingsTransfer", "This backup is damaged and can't be restored.");
}

// Preferences never written to a backup: credentials and per-machine window layout.
bool isExportable(const QString &key)
{
    const QString k = key.toLower();
    const QStringList segments = k.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty())
        return false;
    for (const QString &segment : segments) {
        for (const char *word : {"secret", "password", "passwd", "token", "apikey", "api_key", "privatekey",
                                 "private_key", "credential"}) {
            if (segment.contains(QLatin1String(word)))
                return false;
        }
        if (segment == QLatin1String("key"))
            return false;
    }
    const QString &last = segments.last();
    return !last.contains(QLatin1String("geometry")) && !last.contains(QLatin1String("windowstate"));
}

QJsonValue toJson(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::QByteArray:
        return QJsonObject{{QLatin1String(kBytesTag), QString::fromLatin1(value.toByteArray().toBase64())}};
    case QMetaType::QStringList:
        return QJsonArray::fromStringList(value.toStringList());
    case QMetaType::QVariantList: {
        QJsonArray arr;
        for (const QVariant &v : value.toList())
            arr.append(toJson(v));
        return arr;
    }
    case QMetaType::QVariantMap: {
        QJsonObject o;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            o.insert(it.key(), toJson(it.value()));
        return o;
    }
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return value.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    case QMetaType::QString:
        return value.toString();
    default:
        break;
    }
    const QJsonValue v = QJsonValue::fromVariant(value);
    if (!v.isNull() || value.isNull())
        return v;
    if (value.canConvert<QString>())
        return value.toString();
    return QJsonValue(QJsonValue::Undefined);
}

QVariant fromJson(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (o.size() == 1 && o.value(QLatin1String(kBytesTag)).isString())
            return QByteArray::fromBase64(o.value(QLatin1String(kBytesTag)).toString().toLatin1());
        QVariantMap map;
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            map.insert(it.key(), fromJson(it.value()));
        return map;
    }
    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        bool allStrings = true;
        QVariantList list;
        for (const QJsonValue v : arr) {
            allStrings = allStrings && v.isString();
            list << fromJson(v);
        }
        if (allStrings) {
            QStringList strings;
            for (const QJsonValue v : arr)
                strings << v.toString();
            return strings;
        }
        return list;
    }
    return value.toVariant();
}

// Settings stored as text (INI files) come back as strings; use the type of the
// built-in default where there is one, so the backup has real numbers and booleans.
QVariant typedValue(const QString &key, const QVariant &value)
{
    const QVariant def = Settings::defaultValue(key.toLatin1().constData());
    if (!def.isValid() || value.metaType() == def.metaType() || value.metaType().id() != QMetaType::QString)
        return value;
    QVariant converted = value;
    if (converted.convert(def.metaType()))
        return converted;
    return value;
}

} // namespace

namespace SettingsTransfer {

QStringList dataFiles()
{
    return {QStringLiteral("phrases.json"), QStringLiteral("presets.json"), QStringLiteral("sounds.json"),
            QStringLiteral("sounds/sounds.json"), QStringLiteral("predictor.json")};
}

bool exportTo(const QString &filePath, Settings *settings, QString *error)
{
    QJsonObject values;
    if (settings) {
        settings->sync();
        QStringList keys = settings->allKeys();
        keys.sort();
        for (const QString &key : std::as_const(keys)) {
            if (!isExportable(key))
                continue;
            const QJsonValue v = toJson(typedValue(key, settings->value(key, QVariant())));
            if (!v.isUndefined())
                values.insert(key, v);
        }
    }

    QJsonObject files;
    const QDir dir(Paths::dataDir());
    for (const QString &name : dataFiles()) {
        QFile f(dir.filePath(name));
        if (!f.exists())
            continue;
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QCoreApplication::translate("SettingsTransfer", "Could not read %1.")
                             .arg(QDir::toNativeSeparators(f.fileName()));
            return false;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            files.insert(name, doc.object());
        else if (doc.isArray())
            files.insert(name, doc.array());
    }

    const QJsonObject root{
        {QStringLiteral("app"), appName()},
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("settings"), values},
        {QStringLiteral("files"), files},
    };
    QSaveFile out(filePath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = writeError(filePath, out.errorString());
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (error)
            *error = writeError(filePath, out.errorString());
        return false;
    }
    return true;
}

bool importFrom(const QString &filePath, Settings *settings, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly))
        return fail(QCoreApplication::translate("SettingsTransfer", "Could not open %1: %2")
                        .arg(QDir::toNativeSeparators(filePath), in.errorString()));
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parseError);
    const QJsonObject root = doc.object();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()
        || root.value(QStringLiteral("app")).toString() != appName())
        return fail(QCoreApplication::translate("SettingsTransfer", "This file is not a Vocal Ink backup."));
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version > kFormatVersion)
        return fail(QCoreApplication::translate("SettingsTransfer", "This backup was made by a newer version of "
                                                                    "Vocal Ink. Please update the app first."));
    if (version < 1 || !root.value(QStringLiteral("settings")).isObject()
        || (root.contains(QStringLiteral("files")) && !root.value(QStringLiteral("files")).isObject()))
        return fail(damagedError());

    // Write every data file first (atomically, each via a temporary file); settings
    // only change once all of them are in place.
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    const QDir dir(Paths::dataDir());
    QList<QPair<QString, QByteArray>> contents;
    for (const QString &name : dataFiles()) {
        const QJsonValue content = files.value(name);
        if (content.isUndefined())
            continue;
        if (!content.isObject() && !content.isArray())
            return fail(damagedError());
        const QJsonDocument fileDoc = content.isObject() ? QJsonDocument(content.toObject())
                                                         : QJsonDocument(content.toArray());
        contents.append({dir.filePath(name), fileDoc.toJson(QJsonDocument::Indented)});
    }
    std::vector<std::unique_ptr<QSaveFile>> pending;
    for (const auto &[path, bytes] : std::as_const(contents)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        auto out = std::make_unique<QSaveFile>(path);
        if (!out->open(QIODevice::WriteOnly) || out->write(bytes) != bytes.size())
            return fail(writeError(path, out->errorString()));
        pending.push_back(std::move(out));
    }
    for (const auto &out : pending) {
        if (!out->commit())
            return fail(writeError(out->fileName(), out->errorString()));
    }

    if (settings) {
        const QJsonObject values = root.value(QStringLiteral("settings")).toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (isExportable(it.key()))
                settings->setValue(it.key(), fromJson(it.value()));
        }
        settings->sync();
    }
    return true;
}

} // namespace SettingsTransfer
