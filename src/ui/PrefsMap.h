#pragma once

#include <QQmlPropertyMap>

class Settings;

// Settings as a live QML map: `App.prefs["ui/theme"]` reads a value and
// re-evaluates bindings when it changes; assigning writes it back.
class PrefsMap : public QQmlPropertyMap
{
    Q_OBJECT
public:
    explicit PrefsMap(Settings *settings, QObject *parent = nullptr);

    // For keys without a central default (e.g. "keybinds/speak.stop").
    Q_INVOKABLE QVariant get(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE void reset(const QString &key);
    Q_INVOKABLE QVariant defaultOf(const QString &key) const;

protected:
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    void refresh(const QString &key);

    Settings *m_settings;
    bool m_writing = false;
};
