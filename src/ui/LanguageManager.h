#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;
class QTranslator;
class Settings;

// Picks the interface language (Keys::Language, "" = the system's) and swaps
// translators at run time. Translations live in the app's resources at
// :/i18n/vocalink_<code>.qm, with Qt's own qtbase_/qtdeclarative_<code>.qm
// next to them when the build found them.
//
// Only the interface follows this language: spoken text ({time}, emoji names,
// test sentences) follows the voice.
class LanguageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString current READ current NOTIFY languageChanged)  // code in use, e.g. "de"
    Q_PROPERTY(QString choice READ choice NOTIFY languageChanged)    // the setting ("" = system)
    Q_PROPERTY(QString system READ systemLanguage CONSTANT)          // what the system asks for
    Q_PROPERTY(QVariantList available READ available CONSTANT)      // [{code, name, english}]
public:
    // Settings are read at construction (translators must be installed before
    // any service builds its strings); attach() follows later changes.
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    void attach(Settings *settings, QQmlEngine *engine);
    // For this run only (tests and screenshots: --lang); not saved.
    void setOverride(const QString &code);

    QString current() const { return m_current; }
    QString choice() const { return m_choice; }
    QString systemLanguage() const;
    QVariantList available() const;

    Q_INVOKABLE void choose(const QString &code); // saves Keys::Language

signals:
    void languageChanged();

private:
    void apply(const QString &code);
    void removeTranslators();

    QPointer<Settings> m_settings;
    QPointer<QQmlEngine> m_engine;
    QList<QTranslator *> m_translators;
    QString m_current;
    QString m_choice;
    QString m_override;
};
