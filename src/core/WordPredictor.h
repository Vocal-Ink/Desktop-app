#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

// Word suggestions for faster typing (a core AAC feature). Combines a built-in
// frequency list with what the user actually says (unigrams + bigrams +
// trigrams learned from sent messages), persisted as JSON.
class WordPredictor : public QObject
{
    Q_OBJECT
public:
    explicit WordPredictor(const QString &storePath, QObject *parent = nullptr);
    ~WordPredictor() override;

    void load();
    void save() const;

    // Learn from a message the user actually said.
    void learn(const QString &sentence);

    // Suggestions for the text before the caret: completions of the word being
    // typed, or likely next words after a space. Case follows the input.
    QStringList suggest(const QString &textBeforeCursor, int count = 5) const;

    // Replaces the partial word at the end of `textBeforeCursor` with `word`
    // (or appends it after a space) and returns the new text.
    static QString applySuggestion(const QString &textBeforeCursor, const QString &word);

    void clearLearned();
    int learnedWordCount() const;

signals:
    void learnedChanged();

private:
    class Private;
    Private *d;
    QString m_path;
};
