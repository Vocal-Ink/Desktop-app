#include "platform/GlobalHotkeys.h"

#include <QGuiApplication>

#ifdef VOCALINK_HAVE_HOTKEYS
#include <QHotkey>
#endif

GlobalHotkeys::GlobalHotkeys(QObject *parent)
    : QObject(parent)
{
}

GlobalHotkeys::~GlobalHotkeys()
{
    clear();
}

bool GlobalHotkeys::isSupported()
{
#ifdef VOCALINK_HAVE_HOTKEYS
    const QString platform = QGuiApplication::platformName();
    return !platform.startsWith(QLatin1String("wayland")) && platform != QLatin1String("offscreen")
        && platform != QLatin1String("minimal");
#else
    return false;
#endif
}

QString GlobalHotkeys::unsupportedReason()
{
#ifdef VOCALINK_HAVE_HOTKEYS
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return tr("Wayland does not let apps register system-wide shortcuts. Run Vocal Ink with "
                  "QT_QPA_PLATFORM=xcb (XWayland) to use them, or use your desktop's own shortcut "
                  "settings to launch the quick-type box.");
    if (!isSupported())
        return tr("System-wide shortcuts are not available on this display server.");
    return {};
#else
    return tr("This build was made without system-wide shortcut support.");
#endif
}

bool GlobalHotkeys::set(const QString &id, const QKeySequence &sequence)
{
    remove(id);
    m_failed.removeAll(id);
    if (sequence.isEmpty() || !isSupported())
        return sequence.isEmpty();
#ifdef VOCALINK_HAVE_HOTKEYS
    auto *hotkey = new QHotkey(sequence, false, this);
    connect(hotkey, &QHotkey::activated, this, [this, id] { emit pressed(id); });
    connect(hotkey, &QHotkey::released, this, [this, id] { emit released(id); });
    if (!hotkey->setRegistered(true)) {
        delete hotkey;
        m_failed << id;
        return false;
    }
    m_hotkeys.insert(id, hotkey);
    return true;
#else
    return false;
#endif
}

void GlobalHotkeys::remove(const QString &id)
{
    if (QObject *o = m_hotkeys.take(id)) {
#ifdef VOCALINK_HAVE_HOTKEYS
        static_cast<QHotkey *>(o)->setRegistered(false);
#endif
        delete o;
    }
}

void GlobalHotkeys::clear()
{
    const QStringList ids = m_hotkeys.keys();
    for (const QString &id : ids)
        remove(id);
    m_failed.clear();
}
