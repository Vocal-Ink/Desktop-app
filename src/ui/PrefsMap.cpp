#include "ui/PrefsMap.h"

#include "core/Settings.h"

namespace {
// QML hands numbers over as double and lists as QVariantList; keep the stored
// type stable so QSettings round-trips cleanly.
QVariant coerce(const QVariant &input, const QVariant &like)
{
    if (!like.isValid())
        return input;
    switch (like.typeId()) {
    case QMetaType::Int:
        return input.toInt();
    case QMetaType::Bool:
        return input.toBool();
    case QMetaType::QString:
        return input.toString();
    case QMetaType::Double:
        return input.toDouble();
    case QMetaType::QStringList:
        // A one-item list comes back from INI as a plain string.
        return input.typeId() == QMetaType::QString && input.toString().isEmpty() ? QStringList() : input.toStringList();
    case QMetaType::QByteArray:
        return input.typeId() == QMetaType::QString ? QByteArray::fromHex(input.toString().toLatin1())
                                                   : input.toByteArray();
    case QMetaType::QVariantMap:
        return input.toMap();
    default:
        return input;
    }
}

// Device ids are raw bytes; QML sees them as hex strings.
QVariant exposed(const QVariant &value)
{
    if (value.typeId() == QMetaType::QByteArray)
        return QString::fromLatin1(value.toByteArray().toHex());
    return value;
}
} // namespace

PrefsMap::PrefsMap(Settings *settings, QObject *parent)
    : QQmlPropertyMap(this, parent)
    , m_settings(settings)
{
    const QStringList keys = Settings::knownKeys();
    for (const QString &key : keys)
        refresh(key);
    connect(m_settings, &Settings::changed, this, [this](const QString &key) {
        if (!m_writing)
            refresh(key);
    });
}

void PrefsMap::refresh(const QString &key)
{
    // INI files hand everything back as strings ("true", "4455"); restore the
    // type of the default so QML sees real booleans and numbers.
    const QVariant fallback = Settings::defaultValue(key);
    insert(key, exposed(coerce(m_settings->value(key, fallback), fallback)));
}

QVariant PrefsMap::updateValue(const QString &key, const QVariant &input)
{
    const QVariant value = coerce(input, Settings::defaultValue(key));
    m_writing = true;
    m_settings->setValue(key, value);
    m_writing = false;
    return exposed(value);
}

QVariant PrefsMap::get(const QString &key, const QVariant &fallback) const
{
    const QVariant def = Settings::defaultValue(key);
    return exposed(coerce(m_settings->value(key, def.isValid() ? def : fallback), def));
}

void PrefsMap::set(const QString &key, const QVariant &value)
{
    const QVariant v = coerce(value, Settings::defaultValue(key));
    m_writing = true;
    m_settings->setValue(key, v);
    m_writing = false;
    insert(key, exposed(v));
}

void PrefsMap::reset(const QString &key)
{
    m_settings->remove(key);
    refresh(key);
}

QVariant PrefsMap::defaultOf(const QString &key) const
{
    return exposed(Settings::defaultValue(key));
}
