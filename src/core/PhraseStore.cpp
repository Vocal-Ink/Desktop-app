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
    const QStringList texts = {
        QStringLiteral("Yes"),
        QStringLiteral("No"),
        QStringLiteral("One moment, I'm typing."),
        QStringLiteral("Thank you!"),
        QStringLiteral("Hello, I use text-to-speech to talk."),
        QStringLiteral("Could you repeat that, please?"),
        QStringLiteral("I agree."),
        QStringLiteral("I'll be right back."),
    };
    QList<Phrase> list;
    for (int i = 0; i < texts.size(); ++i) {
        Phrase p;
        p.text = texts.at(i);
        if (i < 4)
            p.hotkey = QStringLiteral("Ctrl+Alt+%1").arg(i + 1);
        list << p;
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
