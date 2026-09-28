#include "core/WordPredictor.h"

// Placeholder implementation (replaced by the text work package).
class WordPredictor::Private {};

WordPredictor::WordPredictor(const QString &storePath, QObject *parent)
    : QObject(parent), d(new Private), m_path(storePath) {}
WordPredictor::~WordPredictor() { delete d; }
void WordPredictor::load() {}
void WordPredictor::save() const {}
void WordPredictor::learn(const QString &) {}
QStringList WordPredictor::suggest(const QString &, int) const { return {}; }
QString WordPredictor::applySuggestion(const QString &textBeforeCursor, const QString &word)
{
    QString t = textBeforeCursor;
    int i = int(t.size());
    while (i > 0 && t.at(i - 1).isLetterOrNumber())
        --i;
    return t.left(i) + word + QLatin1Char(' ');
}
void WordPredictor::clearLearned() {}
int WordPredictor::learnedWordCount() const { return 0; }
