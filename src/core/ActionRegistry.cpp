#include "core/ActionRegistry.h"

#include "core/Settings.h"

#include <QKeySequence>

QList<ActionDef> ActionRegistry::builtInActions()
{
    const QString speaking = tr("Speaking");
    const QString listening = tr("Speech input");
    const QString mic = tr("Your real microphone");
    const QString voices = tr("Voices");
    const QString windows = tr("Windows");
    const QString stream = tr("Stream");
    const QString micWarning = tr("This puts your real microphone on the voice output. People in your call or "
                                  "stream will hear you while it's live.");
    QList<ActionDef> list = {
        {QStringLiteral("speak.stop"), speaking, tr("Stop speaking"),
         tr("Stops the current message and clears the queue."), QStringLiteral("Ctrl+Alt+S"), true, false, {}},
        {QStringLiteral("speak.skip"), speaking, tr("Skip to the next message"),
         tr("Stops the current message; waiting messages continue."), QStringLiteral("Ctrl+Alt+X"), true, false, {}},
        {QStringLiteral("speak.repeat"), speaking, tr("Repeat the last message"),
         tr("Says your last message again."), QStringLiteral("Ctrl+Alt+R"), true, false, {}},
        {QStringLiteral("speak.clipboard"), speaking, tr("Say what's on the clipboard"),
         tr("Speaks the text you last copied."), QString(), true, false, {}},
        {QStringLiteral("listen.ptt"), listening, tr("Push-to-talk (hold)"),
         tr("Hold to speak; your words are written down when you let go."), QStringLiteral("Ctrl+Alt+Space"), true, true, {}},
        {QStringLiteral("listen.toggle"), listening, tr("Start / stop listening"),
         tr("Press once to start, again to transcribe."), QString(), true, false, {}},
        {QStringLiteral("listen.cancel"), listening, tr("Cancel listening"),
         tr("Stops listening without transcribing."), QString(), true, false, {}},
        {QStringLiteral("mic.hold"), mic, tr("Real mic: live while held"),
         tr("Your real microphone joins the voice output only while you hold the keys."), QString(), true, true, micWarning},
        {QStringLiteral("mic.toggle"), mic, tr("Real mic: on / off"),
         tr("Turns your real microphone on or off on the voice output."), QString(), true, false, micWarning},
        {QStringLiteral("panic.mute"), mic, tr("Mute everything"),
         tr("Instantly silences the voice, sounds and your real microphone."), QStringLiteral("Ctrl+Alt+M"), true, false, {}},
        {QStringLiteral("voice.next"), voices, tr("Next favourite voice"),
         tr("Cycles forward through your favourite voices."), QString(), true, false, {}},
        {QStringLiteral("voice.prev"), voices, tr("Previous favourite voice"),
         tr("Cycles back through your favourite voices."), QString(), true, false, {}},
        {QStringLiteral("rate.up"), voices, tr("Speak faster"), tr("Raises the speed by 10%."), QString(), true, false, {}},
        {QStringLiteral("rate.down"), voices, tr("Speak slower"), tr("Lowers the speed by 10%."), QString(), true, false, {}},
        {QStringLiteral("volume.up"), voices, tr("Louder"), tr("Raises the voice volume by 10%."), QString(), true, false, {}},
        {QStringLiteral("volume.down"), voices, tr("Quieter"), tr("Lowers the voice volume by 10%."), QString(), true, false, {}},
        {QStringLiteral("effect.cycle"), voices, tr("Next voice effect"),
         tr("Cycles through voice effects (radio, robot, echo...)."), QString(), true, false, {}},
        {QStringLiteral("window.quickType"), windows, tr("Quick-type box"),
         tr("A small box over any app or game: type, press Enter, done."), QStringLiteral("Ctrl+Alt+T"), true, false, {}},
        {QStringLiteral("window.toggle"), windows, tr("Show / hide Vocal Ink"),
         tr("Brings the main window forward, or hides it."), QStringLiteral("Ctrl+Alt+V"), true, false, {}},
        {QStringLiteral("window.compact"), windows, tr("Compact bar"),
         tr("Switches between the full window and the floating bar."), QString(), true, false, {}},
        {QStringLiteral("window.showText"), windows, tr("Show text full screen"),
         tr("Shows your last message in giant letters for someone next to you."), QString(), true, false, {}},
        {QStringLiteral("window.palette"), windows, tr("Command palette"),
         tr("Search every action, phrase, voice and setting."), QStringLiteral("Ctrl+K"), false, false, {}},
        {QStringLiteral("stream.clearCaptions"), stream, tr("Clear captions"),
         tr("Removes the caption from the overlay and OBS right away."), QString(), true, false, {}},
        {QStringLiteral("stream.toggleCaptions"), stream, tr("Pause captions"),
         tr("Stops (or resumes) sending what you say to the stream."), QString(), true, false, {}},
    };
    for (int i = 1; i <= 5; ++i) {
        list.append({QStringLiteral("voice.fav%1").arg(i), voices, tr("Favourite voice %1").arg(i),
                     tr("Switches to favourite voice number %1.").arg(i), QString(), true, false, {}});
    }
    for (int i = 1; i <= 3; ++i) {
        list.append({QStringLiteral("preset.%1").arg(i), voices, tr("Voice preset %1").arg(i),
                     tr("Applies voice preset number %1 (voice, speed, pitch and effect).").arg(i), QString(), true,
                     false, {}});
    }
    return list;
}

ActionRegistry::ActionRegistry(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_actions(builtInActions())
{
}

void ActionRegistry::retranslate()
{
    m_actions = builtInActions();
    emit actionsChanged();
}

QString ActionRegistry::settingsKey(const QString &id)
{
    return QStringLiteral("keybinds/") + id;
}

ActionDef ActionRegistry::action(const QString &id) const
{
    for (const ActionDef &a : m_actions) {
        if (a.id == id)
            return a;
    }
    return {};
}

bool ActionRegistry::contains(const QString &id) const
{
    return !action(id).id.isEmpty();
}

QStringList ActionRegistry::categories() const
{
    QStringList list;
    for (const ActionDef &a : m_actions) {
        if (!list.contains(a.category))
            list << a.category;
    }
    return list;
}

QString ActionRegistry::shortcut(const QString &id) const
{
    const ActionDef a = action(id);
    if (a.id.isEmpty())
        return {};
    return m_settings->value(settingsKey(id), a.defaultShortcut).toString();
}

void ActionRegistry::setShortcut(const QString &id, const QString &sequence)
{
    if (!contains(id))
        return;
    const QString normalized = normalize(sequence);
    if (shortcut(id) == normalized && m_settings->contains(settingsKey(id)))
        return;
    m_settings->setValue(settingsKey(id), normalized);
    emit shortcutChanged(id);
}

void ActionRegistry::resetToDefault(const QString &id)
{
    if (!m_settings->contains(settingsKey(id)))
        return;
    m_settings->remove(settingsKey(id));
    emit shortcutChanged(id);
}

void ActionRegistry::resetAll()
{
    for (const ActionDef &a : std::as_const(m_actions))
        m_settings->remove(settingsKey(a.id));
    emit shortcutsReset();
}

bool ActionRegistry::isDefault(const QString &id) const
{
    return shortcut(id) == action(id).defaultShortcut;
}

void ActionRegistry::setExtraBindings(const QList<QPair<QString, QString>> &labelAndSequence)
{
    m_extra = labelAndSequence;
}

QStringList ActionRegistry::conflictsWith(const QString &id, const QString &sequence) const
{
    const QString seq = normalize(sequence);
    if (seq.isEmpty())
        return {};
    QStringList names;
    for (const ActionDef &a : m_actions) {
        if (a.id != id && shortcut(a.id) == seq)
            names << a.title;
    }
    for (const auto &extra : m_extra) {
        if (extra.first != id && normalize(extra.second) == seq)
            names << extra.first;
    }
    return names;
}

QString ActionRegistry::normalize(const QString &sequence)
{
    const QKeySequence seq = QKeySequence::fromString(sequence.trimmed(), QKeySequence::PortableText);
    if (seq.isEmpty())
        return {};
    return QKeySequence(seq[0]).toString(QKeySequence::PortableText);
}
