#include "core/PhraseStore.h"

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
        {"Basics", "Yes", "Ctrl+Alt+1"},
        {"Basics", "No", "Ctrl+Alt+2"},
        {"Basics", "One moment, I'm typing.", "Ctrl+Alt+3"},
        {"Basics", "Thank you!", "Ctrl+Alt+4"},
        {"Basics", "Could you repeat that, please?", ""},
        {"Basics", "I use text-to-speech to talk. Give me a second to type.", ""},
        {"Basics", "I agree.", ""},
        {"Basics", "I'll be right back.", ""},
        {"Social", "Hi! How are you?", ""},
        {"Social", "That's so funny.", ""},
        {"Social", "Nice to meet you.", ""},
        {"Social", "Sorry, I missed that.", ""},
        {"Social", "Good night, everyone!", ""},
        {"Stream", "Welcome in! Thanks for stopping by.", ""},
        {"Stream", "Thank you so much for the follow!", ""},
        {"Stream", "Thanks for the raid, welcome raiders!", ""},
        {"Stream", "Taking a quick break, back in five.", ""},
        {"Stream", "Clip that!", ""},
        {"Games", "Nice shot!", ""},
        {"Games", "Enemy over here!", ""},
        {"Games", "I need help.", ""},
        {"Games", "Good game, everyone.", ""},
    };
    QList<Phrase> list;
    for (const Seed &s : seeds) {
        Phrase p;
        p.category = QString::fromLatin1(s.category);
        p.text = QString::fromUtf8(s.text);
        p.hotkey = QString::fromLatin1(s.hotkey);
        list << p;
    }
    return list;
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
