#include "core/TextProcessor.h"

#include <QRegularExpression>

namespace TextProcessor {

QString expandReplacements(const QString &text, const QVariantMap &replacements)
{
    if (replacements.isEmpty() || text.isEmpty())
        return text;

    // Tokenise into word / non-word runs so punctuation stays where it was.
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}\\p{N}_']+"));
    QHash<QString, QString> lookup;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!key.isEmpty())
            lookup.insert(key, it.value().toString());
    }

    QString out;
    out.reserve(text.size() + 16);
    qsizetype last = 0;
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString word = m.captured();
        const auto found = lookup.constFind(word.toLower());
        if (found == lookup.cend()) {
            out += word;
        } else {
            QString expansion = found.value();
            if (!expansion.isEmpty() && word.at(0).isUpper() && expansion.at(0).isLower())
                expansion[0] = expansion.at(0).toUpper();
            out += expansion;
        }
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QString normalizeForSpeech(const QString &text)
{
    QString s;
    s.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r') || c == QLatin1Char('\t'))
            s += QLatin1Char(' ');
        else if (c.category() == QChar::Other_Control)
            continue;
        else
            s += c;
    }
    s = s.simplified();
    static const QRegularExpression speakable(QStringLiteral("[\\p{L}\\p{N}]"));
    if (!speakable.match(s).hasMatch())
        return QString();
    return s;
}

QStringList splitForSpeech(const QString &input, int minChars, int maxChars)
{
    const QString text = normalizeForSpeech(input);
    if (text.isEmpty())
        return {};
    if (text.size() <= maxChars && text.size() <= minChars * 3)
        return {text};

    // 1. Sentence boundaries: terminal punctuation followed by whitespace.
    static const QRegularExpression boundary(QStringLiteral("(?<=[.!?…。！？])\\s+"));
    const QStringList sentences = text.split(boundary, Qt::SkipEmptyParts);

    // 2. Break over-long sentences at commas/semicolons, then at spaces.
    QStringList pieces;
    for (const QString &sentence : sentences) {
        if (sentence.size() <= maxChars) {
            pieces << sentence;
            continue;
        }
        static const QRegularExpression soft(QStringLiteral("(?<=[,;:—])\\s+"));
        QString current;
        const QStringList clauses = sentence.split(soft, Qt::SkipEmptyParts);
        for (const QString &clause : clauses) {
            const QStringList words = clause.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &word : words) {
                if (!current.isEmpty() && current.size() + 1 + word.size() > maxChars) {
                    pieces << current;
                    current.clear();
                }
                current += current.isEmpty() ? word : QLatin1Char(' ') + word;
            }
            // Prefer a break at the end of a clause if the chunk is already long enough.
            if (current.size() >= minChars) {
                pieces << current;
                current.clear();
            }
        }
        if (!current.isEmpty())
            pieces << current;
    }

    // 3. Merge tiny pieces with their neighbour so prosody doesn't get choppy.
    QStringList merged;
    for (const QString &piece : pieces) {
        if (!merged.isEmpty()
            && (merged.last().size() < minChars || piece.size() < minChars / 2)
            && merged.last().size() + 1 + piece.size() <= maxChars) {
            merged.last() += QLatin1Char(' ') + piece;
        } else {
            merged << piece;
        }
    }
    return merged;
}

QString cleanTranscript(const QString &text)
{
    QString s = text;
    static const QRegularExpression bracketed(QStringLiteral("\\[[^\\]]*\\]|\\([^\\)]*\\)|\\*[^*]*\\*"));
    s.remove(bracketed);
    static const QRegularExpression music(QStringLiteral("[♪♫♬♭♮♯]"));
    s.remove(music);
    s = s.simplified();
    // Whisper sometimes emits a lone punctuation mark or "-" for silence.
    static const QRegularExpression letters(QStringLiteral("[\\p{L}\\p{N}]"));
    if (!letters.match(s).hasMatch())
        return QString();
    return s;
}

QStringList vocabularyFrom(const QString &text)
{
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}']{3,}"));
    QStringList words;
    auto it = wordRe.globalMatch(text);
    while (it.hasNext())
        words << it.next().captured().toLower();
    return words;
}

} // namespace TextProcessor
