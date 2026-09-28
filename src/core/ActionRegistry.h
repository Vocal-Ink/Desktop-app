#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class Settings;

// Everything the user can bind a key to. Built-in actions live here; quick
// phrases and soundboard sounds keep their own shortcuts but take part in
// conflict checks through extraBindings().
struct ActionDef
{
    QString id;              // e.g. "speak.stop"
    QString category;        // e.g. "Speaking"
    QString title;           // e.g. "Stop speaking"
    QString description;     // one line, shown under the title
    QString defaultShortcut; // QKeySequence portable text, may be empty
    bool global = true;      // works while other apps are focused
    bool hold = false;       // acts on press *and* release (push-to-talk)
    QString warning;         // shown when binding it (e.g. real mic goes live)
};

class ActionRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ActionRegistry(Settings *settings, QObject *parent = nullptr);

    static QList<ActionDef> builtInActions();
    QList<ActionDef> actions() const { return m_actions; }
    ActionDef action(const QString &id) const;
    bool contains(const QString &id) const;
    QStringList categories() const;

    QString shortcut(const QString &id) const; // portable text, "" = unbound
    void setShortcut(const QString &id, const QString &sequence);
    void resetToDefault(const QString &id);
    void resetAll();
    bool isDefault(const QString &id) const;

    // Shortcuts owned elsewhere (quick phrases, sounds): label -> sequence.
    void setExtraBindings(const QList<QPair<QString, QString>> &labelAndSequence);
    // Human-readable names of everything else already using `sequence`.
    QStringList conflictsWith(const QString &id, const QString &sequence) const;

    // Rebuilds titles and descriptions in the current language.
    void retranslate();

    // Normalises user input to a single-chord portable string ("Ctrl+Alt+T").
    static QString normalize(const QString &sequence);

signals:
    void shortcutChanged(const QString &id);
    void shortcutsReset();
    void actionsChanged(); // titles/descriptions changed (language)

private:
    static QString settingsKey(const QString &id);

    Settings *m_settings;
    QList<ActionDef> m_actions;
    QList<QPair<QString, QString>> m_extra;
};
