#include "core/PhraseStore.h"

#include <QCoreApplication>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

PhraseStore::PhraseStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath)
{
}

QList<Phrase> PhraseStore::defaultPhrases()
{
    struct Seed
    {
        const char *category;
        const char *text;
        const char *hotkey;
    };
    static const Seed seeds[] = {
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "Yes"), "Ctrl+Alt+1"},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "No"), "Ctrl+Alt+2"},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "One moment, I'm typing."), "Ctrl+Alt+3"},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "Thank you!"), "Ctrl+Alt+4"},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "Could you repeat that, please?"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "I use text-to-speech to talk. Give me a second to type."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "I agree."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Basics"),
         QT_TRANSLATE_NOOP("PhraseStore", "I'll be right back."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Social"),
         QT_TRANSLATE_NOOP("PhraseStore", "Hi! How are you?"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Social"),
         QT_TRANSLATE_NOOP("PhraseStore", "That's so funny."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Social"),
         QT_TRANSLATE_NOOP("PhraseStore", "Nice to meet you."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Social"),
         QT_TRANSLATE_NOOP("PhraseStore", "Sorry, I missed that."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Social"),
         QT_TRANSLATE_NOOP("PhraseStore", "Good night, everyone!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Stream"),
         QT_TRANSLATE_NOOP("PhraseStore", "Welcome in! Thanks for stopping by."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Stream"),
         QT_TRANSLATE_NOOP("PhraseStore", "Thank you so much for the follow!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Stream"),
         QT_TRANSLATE_NOOP("PhraseStore", "Thanks for the raid, welcome raiders!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Stream"),
         QT_TRANSLATE_NOOP("PhraseStore", "Taking a quick break, back in five."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Stream"),
         QT_TRANSLATE_NOOP("PhraseStore", "Clip that!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Games"),
         QT_TRANSLATE_NOOP("PhraseStore", "Nice shot!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Games"),
         QT_TRANSLATE_NOOP("PhraseStore", "Enemy over here!"), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Games"),
         QT_TRANSLATE_NOOP("PhraseStore", "I need help."), ""},
        {QT_TRANSLATE_NOOP("PhraseStore", "Games"),
         QT_TRANSLATE_NOOP("PhraseStore", "Good game, everyone."), ""},
    };
    QList<Phrase> list;
    for (const Seed &s : seeds) {
        Phrase p;
        // Written in the interface language; they're saved as plain text once edited.
        p.category = QCoreApplication::translate("PhraseStore", s.category);
        p.text = QCoreApplication::translate("PhraseStore", s.text);
        p.hotkey = QString::fromLatin1(s.hotkey);
        list << p;
    }
    return list;
}

void PhraseStore::retranslateDefaults()
{
    if (QFile::exists(m_path))
        return; // the user has made them their own
    m_phrases = defaultPhrases();
    emit changed();
}

QStringList PhraseStore::categories() const
{
    QStringList list;
    for (const Phrase &p : m_phrases) {
        const QString c = p.category.isEmpty() ? tr("Basics") : p.category;
        if (!list.contains(c))
            list << c;
    }
    return list;
}

QByteArray PhraseStore::toJson(const QList<Phrase> &phrases)
{
    QJsonArray arr;
    for (const Phrase &p : phrases) {
        QJsonObject o{{QStringLiteral("text"), p.text}};
        if (!p.hotkey.isEmpty())
            o.insert(QStringLiteral("hotkey"), p.hotkey);
        if (!p.voiceKey.isEmpty())
            o.insert(QStringLiteral("voice"), p.voiceKey);
        if (!p.category.isEmpty())
            o.insert(QStringLiteral("category"), p.category);
        if (!p.color.isEmpty())
            o.insert(QStringLiteral("color"), p.color);
        arr.append(o);
    }
    QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("phrases"), arr}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QList<Phrase> PhraseStore::fromJson(const QByteArray &json, bool *ok)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    QList<Phrase> list;
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return list;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("phrases")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Phrase p;
        p.text = o.value(QStringLiteral("text")).toString().trimmed();
        p.hotkey = o.value(QStringLiteral("hotkey")).toString();
        p.voiceKey = o.value(QStringLiteral("voice")).toString();
        p.category = o.value(QStringLiteral("category")).toString();
        p.color = o.value(QStringLiteral("color")).toString();
        if (!p.text.isEmpty())
            list << p;
    }
    if (ok)
        *ok = true;
    return list;
}

bool PhraseStore::load()
{
    QFile f(m_path);
    if (!f.exists()) {
        m_phrases = defaultPhrases();
        emit changed();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const QList<Phrase> list = fromJson(f.readAll(), &ok);
    if (!ok)
        return false;
    m_phrases = list;
    emit changed();
    return true;
}

bool PhraseStore::save() const
{
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(toJson(m_phrases));
    return f.commit();
}

void PhraseStore::commit()
{
    save();
    emit changed();
}

void PhraseStore::setPhrases(const QList<Phrase> &phrases)
{
    m_phrases = phrases;
    commit();
}

void PhraseStore::add(const Phrase &phrase)
{
    m_phrases.append(phrase);
    commit();
}

void PhraseStore::update(int index, const Phrase &phrase)
{
    if (index < 0 || index >= m_phrases.size())
        return;
    m_phrases[index] = phrase;
    commit();
}

void PhraseStore::removeAt(int index)
{
    if (index < 0 || index >= m_phrases.size())
        return;
    m_phrases.removeAt(index);
    commit();
}

void PhraseStore::move(int from, int to)
{
    if (from < 0 || from >= m_phrases.size() || to < 0 || to >= m_phrases.size() || from == to)
        return;
    m_phrases.move(from, to);
    commit();
}
