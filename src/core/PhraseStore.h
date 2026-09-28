#pragma once

#include <QList>
#include <QObject>
#include <QString>

struct Phrase
{
    QString text;
    QString hotkey;   // optional global shortcut (QKeySequence portable text)
    QString voiceKey; // optional per-phrase voice; empty = current voice
    QString category; // board tab, e.g. "Basics"
    QString color;    // optional tile tint "#rrggbb"

    bool operator==(const Phrase &o) const
    {
        return text == o.text && hotkey == o.hotkey && voiceKey == o.voiceKey && category == o.category
            && color == o.color;
    }
};

// Quick phrases the user can say with one click or one key. Persisted as JSON.
class PhraseStore : public QObject
{
    Q_OBJECT
public:
    explicit PhraseStore(const QString &filePath, QObject *parent = nullptr);

    const QList<Phrase> &phrases() const { return m_phrases; }
    void setPhrases(const QList<Phrase> &phrases);
    void add(const Phrase &phrase);
    void update(int index, const Phrase &phrase);
    void removeAt(int index);
    void move(int from, int to);

    bool load();  // falls back to defaultPhrases() if the file doesn't exist
    bool save() const;

    static QList<Phrase> defaultPhrases(); // in the interface language
    // While the starter phrases are untouched (never saved), switch them to
    // the current interface language.
    void retranslateDefaults();
    QStringList categories() const; // in first-use order
    static QByteArray toJson(const QList<Phrase> &phrases);
    static QList<Phrase> fromJson(const QByteArray &json, bool *ok = nullptr);

signals:
    void changed();

private:
    void commit();

    QString m_path;
    QList<Phrase> m_phrases;
};
