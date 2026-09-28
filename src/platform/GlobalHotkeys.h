#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

// System-wide shortcuts (work while a game or another app has focus).
// Backed by QHotkey: Windows, macOS and X11. Wayland does not allow global
// shortcuts for regular apps, so isSupported() is false there.
class GlobalHotkeys : public QObject
{
    Q_OBJECT
public:
    explicit GlobalHotkeys(QObject *parent = nullptr);
    ~GlobalHotkeys() override;

    static bool isSupported();
    static QString unsupportedReason();

    // Registers `sequence` under `id`, replacing the previous binding for that id.
    // An empty sequence just removes the binding. Returns false if the OS refused it.
    bool set(const QString &id, const QKeySequence &sequence);
    void remove(const QString &id);
    void clear();
    QStringList failedIds() const { return m_failed; }

signals:
    void pressed(const QString &id);
    void released(const QString &id);

private:
    QHash<QString, QObject *> m_hotkeys;
    QStringList m_failed;
};
