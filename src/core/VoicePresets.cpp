#include "core/VoicePresets.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <utility>

VoicePresets::VoicePresets(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
}

QByteArray VoicePresets::toJson(const QList<VoicePreset> &presets)
{
    QJsonArray arr;
    for (const VoicePreset &p : presets) {
        arr.append(QJsonObject{
            {QStringLiteral("id"), p.id},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("voice"), p.voiceKey},
            {QStringLiteral("rate"), p.rate},
            {QStringLiteral("pitch"), p.pitch},
            {QStringLiteral("effect"), p.effect},
            {QStringLiteral("effectIntensity"), p.effectIntensity},
        });
    }
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("presets"), arr}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QList<VoicePreset> VoicePresets::fromJson(const QByteArray &json, bool *ok)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    QList<VoicePreset> list;
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return list;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("presets")).toArray();
    for (const QJsonValue v : arr) {
        const QJsonObject o = v.toObject();
        VoicePreset p;
        p.id = o.value(QStringLiteral("id")).toString().trimmed();
        p.name = o.value(QStringLiteral("name")).toString().trimmed();
        p.voiceKey = o.value(QStringLiteral("voice")).toString();
        p.rate = qBound(50, o.value(QStringLiteral("rate")).toInt(100), 200);
        p.pitch = qBound(-50, o.value(QStringLiteral("pitch")).toInt(0), 50);
        p.effect = o.value(QStringLiteral("effect")).toString();
        p.effectIntensity = qBound(0, o.value(QStringLiteral("effectIntensity")).toInt(60), 100);
        const auto used = [&list](const QString &id) {
            for (const VoicePreset &existing : std::as_const(list)) {
                if (existing.id == id)
                    return true;
            }
            return false;
        };
        while (p.id.isEmpty() || used(p.id))
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        list << p;
    }
    if (ok)
        *ok = true;
    return list;
}

bool VoicePresets::load()
{
    QFile f(m_path);
    if (!f.exists()) {
        m_presets.clear();
        emit changed();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const QList<VoicePreset> list = fromJson(f.readAll(), &ok);
    if (!ok)
        return false;
    m_presets = list;
    emit changed();
    return true;
}

bool VoicePresets::save() const
{
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(toJson(m_presets));
    return f.commit();
}

void VoicePresets::commit()
{
    save();
    emit changed();
}

QString VoicePresets::newId() const
{
    for (;;) {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        bool used = false;
        for (const VoicePreset &p : m_presets)
            used = used || p.id == id;
        if (!used)
            return id;
    }
}

VoicePreset VoicePresets::preset(const QString &id) const
{
    for (const VoicePreset &p : m_presets) {
        if (p.id == id)
            return p;
    }
    return {};
}

QString VoicePresets::add(const VoicePreset &preset)
{
    VoicePreset p = preset;
    p.id = newId();
    m_presets.append(p);
    commit();
    return p.id;
}

void VoicePresets::update(const VoicePreset &preset)
{
    for (VoicePreset &p : m_presets) {
        if (p.id == preset.id) {
            if (p == preset)
                return;
            p = preset;
            commit();
            return;
        }
    }
}

void VoicePresets::remove(const QString &id)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id == id) {
            m_presets.removeAt(i);
            commit();
            return;
        }
    }
}

void VoicePresets::move(int from, int to)
{
    if (from < 0 || from >= m_presets.size() || to < 0 || to >= m_presets.size() || from == to)
        return;
    m_presets.move(from, to);
    commit();
}
