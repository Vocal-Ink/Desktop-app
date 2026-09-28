#include "tts/CloudHelpers.h"

#include "core/NetworkUtil.h"
#include "core/Settings.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QStringList>

namespace CloudTts {

void getJson(QNetworkAccessManager *network, const QNetworkRequest &request, const QString &provider,
             QObject *context, JsonHandler done)
{
    QNetworkReply *reply = network->get(request);
    reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, provider, done = std::move(done)] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || NetworkUtil::httpStatus(reply) >= 300) {
            done({}, NetworkUtil::describeError(reply, body, provider));
            return;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError) {
            done({}, QObject::tr("%1 sent an answer that could not be read").arg(provider));
            return;
        }
        done(doc, {});
    });
}

QList<Voice> customVoices(const Settings *settings, const char *settingsKey, const QString &engineId)
{
    QList<Voice> list;
    if (!settings)
        return list;
    QSet<QString> seen;
    const QStringList entries = settings->value(settingsKey).toStringList();
    for (const QString &entry : entries) {
        const qsizetype bar = entry.indexOf(QLatin1Char('|'));
        Voice v;
        v.engineId = engineId;
        v.id = (bar < 0 ? entry : entry.left(bar)).trimmed();
        v.name = bar < 0 ? QString() : entry.mid(bar + 1).trimmed();
        if (v.id.isEmpty() || seen.contains(v.id))
            continue;
        seen.insert(v.id);
        if (v.name.isEmpty())
            v.name = v.id;
        v.description = QObject::tr("Added by you");
        list.append(v);
    }
    return list;
}

void addCustomVoice(Settings *settings, const char *settingsKey, const QString &id, const QString &name)
{
    const QString cleanId = QString(id).remove(QLatin1Char('|')).trimmed();
    if (!settings || cleanId.isEmpty())
        return;
    const QString cleanName = QString(name).simplified();
    QStringList entries = settings->value(settingsKey).toStringList();
    const QString entry = cleanId + QLatin1Char('|') + (cleanName.isEmpty() ? cleanId : cleanName);
    bool replaced = false;
    for (QString &e : entries) {
        if (e.section(QLatin1Char('|'), 0, 0).trimmed() == cleanId) {
            e = entry;
            replaced = true;
        }
    }
    if (!replaced)
        entries.append(entry);
    settings->setValue(settingsKey, entries);
}

void removeCustomVoice(Settings *settings, const char *settingsKey, const QString &id)
{
    if (!settings)
        return;
    QStringList entries = settings->value(settingsKey).toStringList();
    const qsizetype before = entries.size();
    entries.removeIf([&id](const QString &e) { return e.section(QLatin1Char('|'), 0, 0).trimmed() == id.trimmed(); });
    if (entries.size() != before)
        settings->setValue(settingsKey, entries);
}

void mergeVoices(QList<Voice> &list, const QList<Voice> &extra)
{
    QSet<QString> ids;
    for (const Voice &v : std::as_const(list))
        ids.insert(v.id);
    for (const Voice &v : extra) {
        if (!ids.contains(v.id)) {
            ids.insert(v.id);
            list.append(v);
        }
    }
}

QString normalizeGender(const QString &gender)
{
    const QString g = gender.trimmed().toLower();
    if (g == QLatin1String("female"))
        return QStringLiteral("Female");
    if (g == QLatin1String("male"))
        return QStringLiteral("Male");
    return {};
}

} // namespace CloudTts
