#include "core/WordPredictor.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <utility>
#include <vector>

static void initPredictResources()
{
    // core.qrc is compiled into a static library; referencing it keeps the linker from dropping it.
    Q_INIT_RESOURCE(core);
}

namespace {

constexpr int kMaxUnigrams = 20000;
constexpr int kMaxNgrams = 50000;      // per order (bigrams, trigrams)
constexpr double kLearnedBoost = 1.5;  // per use, on the scale of the base scores (~0.5..5)
constexpr double kNgramBoost = 3.0;    // per learned use, on the scale of the built-in pair counts
constexpr double kContextWeight = 4.0; // how much the previous words steer completions
constexpr double kBackoff = 0.4;       // "stupid backoff" factor from trigrams to bigrams
constexpr double kBaseOffset = 2.5;    // Zipf 2.5 (very rare) maps to 0
constexpr int kContextChars = 300;     // only the end of long texts matters
constexpr int kSaveDelayMs = 2000;
constexpr int kMaxWordLength = 40;

const QString &startToken()
{
    static const QString s = QStringLiteral("<s>");
    return s;
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || isApostrophe(c);
}

QString normalizeApostrophes(QString s)
{
    s.replace(QChar(0x2019), QLatin1Char('\''));
    return s;
}

QString stripApostrophes(QString s)
{
    s.remove(QLatin1Char('\''));
    return s;
}

bool hasDigit(const QString &s)
{
    return std::any_of(s.cbegin(), s.cend(), [](QChar c) { return c.isDigit(); });
}

// Raw word tokens of a piece of text (apostrophes already normalised).
QStringList tokens(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("[\\p{L}\\p{M}\\p{N}]+(?:'[\\p{L}\\p{M}\\p{N}]+)*"));
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        out << it.next().captured();
    return out;
}

// Built-in next-word counts: context ("w1" or "w1 w2") -> next word -> count.
struct BaseNgrams
{
    QHash<QString, QHash<QString, double>> next;
    QHash<QString, double> totals;
};

struct BaseData
{
    QHash<QString, double> scores; // key -> score
    QHash<QString, QString> forms; // key -> display form ("Monday", "TV")
    QStringList top;               // keys, most frequent first
    BaseNgrams bigrams;
    BaseNgrams trigrams;
};

void loadBaseNgrams(const QString &path, BaseNgrams &table)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> parts = line.trimmed().split('\t');
        if (parts.size() != 3)
            continue;
        const QString context = QString::fromUtf8(parts.at(0));
        const double count = parts.at(2).toDouble();
        table.next[context].insert(QString::fromUtf8(parts.at(1)), count);
        table.totals[context] += count;
    }
}

BaseData loadBaseData()
{
    initPredictResources();
    BaseData data;
    QFile words(QStringLiteral(":/predict/en-words.tsv"));
    if (words.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = words.readAll().split('\n');
        data.scores.reserve(lines.size());
        for (const QByteArray &line : lines) {
            const QList<QByteArray> parts = line.trimmed().split('\t');
            if (parts.size() != 2)
                continue;
            const QString form = QString::fromUtf8(parts.at(0));
            const QString key = form.toLower();
            if (key.isEmpty() || data.scores.contains(key))
                continue;
            data.scores.insert(key, qMax(0.1, parts.at(1).toDouble() / 100.0 - kBaseOffset));
            if (form != key)
                data.forms.insert(key, form);
            data.top << key;
        }
    }
    loadBaseNgrams(QStringLiteral(":/predict/en-bigrams.tsv"), data.bigrams);
    loadBaseNgrams(QStringLiteral(":/predict/en-trigrams.tsv"), data.trigrams);
    return data;
}

const BaseData &baseData()
{
    static const BaseData data = loadBaseData();
    return data;
}

// The learned data can be large (tens of thousands of entries), so it is written
// directly instead of through QJsonArray.
void appendJsonString(QByteArray &out, const QString &text)
{
    out += '"';
    const QByteArray utf8 = text.toUtf8();
    for (const char c : utf8) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += "\\u00";
            out += "0123456789abcdef"[(c >> 4) & 0xf];
            out += "0123456789abcdef"[c & 0xf];
        } else {
            out += c;
        }
    }
    out += '"';
}

// Learned next-word counts, with per-context totals so probabilities are cheap.
struct NgramTable
{
    QHash<QString, QHash<QString, int>> next;
    QHash<QString, int> totals;
    int size = 0; // number of (context, word) pairs

    void add(const QString &context, const QString &word, int count = 1)
    {
        int &c = next[context][word];
        if (c == 0)
            ++size;
        c += count;
        totals[context] += count;
    }

    void clear()
    {
        next.clear();
        totals.clear();
        size = 0;
    }

    // Drops the least used pairs once the table grows past its cap.
    void prune(int cap)
    {
        if (size <= cap)
            return;
        struct Item
        {
            int count;
            QString context;
            QString word;
        };
        std::vector<Item> items;
        items.reserve(size_t(size));
        for (auto it = next.cbegin(); it != next.cend(); ++it) {
            for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt)
                items.push_back({jt.value(), it.key(), jt.key()});
        }
        const size_t keep = size_t(cap) * 9 / 10;
        const size_t drop = items.size() > keep ? items.size() - keep : 0;
        const auto leastUsed = [](const Item &a, const Item &b) {
            if (a.count != b.count)
                return a.count < b.count;
            return a.context != b.context ? a.context < b.context : a.word < b.word;
        };
        std::partial_sort(items.begin(), items.begin() + qsizetype(drop), items.end(), leastUsed);
        for (size_t i = 0; i < drop; ++i)
            removePair(items[i].context, items[i].word);
    }

    void removePair(const QString &context, const QString &word)
    {
        const auto it = next.find(context);
        if (it == next.end())
            return;
        const int count = it.value().take(word);
        if (count == 0)
            return;
        --size;
        if (it.value().isEmpty()) {
            next.erase(it);
            totals.remove(context);
        } else {
            totals[context] -= count;
        }
    }

    // Everything that mentions `word`, as the next word or in the context.
    void removeWord(const QString &word)
    {
        for (auto it = next.begin(); it != next.end();) {
            const QStringList parts = it.key().split(QLatin1Char(' '));
            if (parts.contains(word)) {
                size -= int(it.value().size());
                totals.remove(it.key());
                it = next.erase(it);
                continue;
            }
            const int count = it.value().take(word);
            if (count > 0) {
                --size;
                totals[it.key()] -= count;
            }
            if (it.value().isEmpty()) {
                totals.remove(it.key());
                it = next.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Stored as "context word count" strings: compact and quick to write.
    void appendJson(QByteArray &out) const
    {
        out += '[';
        bool first = true;
        for (auto it = next.cbegin(); it != next.cend(); ++it) {
            for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
                if (!first)
                    out += ',';
                first = false;
                appendJsonString(out, it.key() + QLatin1Char(' ') + jt.key() + QLatin1Char(' ')
                                          + QString::number(jt.value()));
            }
        }
        out += ']';
    }

    void fromJson(const QJsonArray &arr, int order)
    {
        for (const QJsonValue v : arr) {
            const QStringList parts =
                normalizeApostrophes(v.toString()).toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() != order + 1)
                continue;
            const int count = parts.last().toInt();
            if (count > 0)
                add(parts.mid(0, order - 1).join(QLatin1Char(' ')), parts.at(order - 1), count);
        }
    }
};

// Probability of a word following a context, from the built-in and learned counts.
class NextWordModel
{
public:
    NextWordModel(const BaseNgrams &base, const NgramTable &learned, const QString &context)
    {
        const auto b = base.next.constFind(context);
        if (b != base.next.cend())
            m_base = &b.value();
        const auto l = learned.next.constFind(context);
        if (l != learned.next.cend())
            m_learned = &l.value();
        m_total = base.totals.value(context) + learned.totals.value(context) * kNgramBoost;
    }

    double probability(const QString &word) const
    {
        if (m_total <= 0)
            return 0;
        const double base = m_base ? m_base->value(word) : 0.0;
        const double learned = m_learned ? m_learned->value(word) * kNgramBoost : 0.0;
        return (base + learned) / m_total;
    }

    template<typename F>
    void forEachWord(F f) const
    {
        if (m_base) {
            for (auto it = m_base->keyBegin(); it != m_base->keyEnd(); ++it)
                f(*it);
        }
        if (m_learned) {
            for (auto it = m_learned->keyBegin(); it != m_learned->keyEnd(); ++it)
                f(*it);
        }
    }

private:
    const QHash<QString, double> *m_base = nullptr;
    const QHash<QString, int> *m_learned = nullptr;
    double m_total = 0;
};

enum class CaseMode { AsIs, Capitalize, Upper };

bool isAllCaps(const QString &word)
{
    int letters = 0;
    for (const QChar c : word) {
        if (c.isLower())
            return false;
        if (c.isLetter())
            ++letters;
    }
    return letters >= 2;
}

QString applyCase(const QString &word, CaseMode mode)
{
    if (word.isEmpty())
        return word;
    switch (mode) {
    case CaseMode::Upper:
        return word.toUpper();
    case CaseMode::Capitalize: {
        // Leave brand-style forms ("iPhone") alone.
        for (qsizetype i = 1; i < word.size(); ++i) {
            if (word.at(i).isUpper())
                return word;
        }
        QString s = word;
        s[0] = s.at(0).toUpper();
        return s;
    }
    case CaseMode::AsIs:
        break;
    }
    return word;
}

// The words before the caret that predict the next one: "<s>" marks the start
// of a sentence. At most the last two are kept.
struct Context
{
    QStringList words;     // lower case
    bool sentenceStart = false;
    bool shouting = false; // the sentence so far is in capitals ("I AM SO ")
};

Context contextOf(const QString &text, bool truncated)
{
    static const QRegularExpression boundary(QStringLiteral("(?:[.!?\\x{2026}]+(?=\\s|$)|\\n)"));
    qsizetype start = 0;
    bool found = false;
    auto it = boundary.globalMatch(text);
    while (it.hasNext()) {
        start = it.next().capturedEnd();
        found = true;
    }
    const QStringList raw = tokens(text.mid(start));
    QStringList words;
    bool atStart = found || !truncated;
    int letters = 0;
    bool shouting = raw.size() >= 2;
    for (const QString &t : raw) {
        for (const QChar c : t) {
            if (c.isLower())
                shouting = false;
            else if (c.isLetter())
                ++letters;
        }
        if (hasDigit(t) || t.size() > kMaxWordLength) {
            words.clear(); // numbers break the chain, as when learning
            atStart = false;
            continue;
        }
        words << t.toLower();
    }
    if (atStart)
        words.prepend(startToken());
    Context ctx;
    ctx.sentenceStart = atStart && raw.isEmpty();
    ctx.shouting = shouting && letters >= 4;
    ctx.words = words.mid(qMax<qsizetype>(0, words.size() - 2));
    return ctx;
}

} // namespace

class WordPredictor::Private
{
public:
    struct Entry
    {
        QString stripped; // key without apostrophes, so "dont" finds "don't"
        QString key;
        bool operator<(const Entry &o) const { return stripped != o.stripped ? stripped < o.stripped : key < o.key; }
    };

    double wordScore(const QString &key) const
    {
        return baseData().scores.value(key) + unigrams.value(key) * kLearnedBoost;
    }

    QString display(const QString &key) const
    {
        const auto learned = forms.constFind(key);
        if (learned != forms.cend())
            return learned.value();
        const auto base = baseData().forms.constFind(key);
        if (base != baseData().forms.cend())
            return base.value();
        if (key == QLatin1String("i") || key.startsWith(QLatin1String("i'")))
            return QLatin1Char('I') + key.mid(1);
        return key;
    }

    // Next-word probabilities from trigrams, backing off to bigrams.
    struct Predictor
    {
        NextWordModel trigram;
        NextWordModel bigram;

        double score(const QString &word) const
        {
            const double p = trigram.probability(word);
            return p > 0 ? p : kBackoff * bigram.probability(word);
        }
    };

    Predictor predictor(const QStringList &context) const
    {
        const qsizetype n = context.size();
        const QString tri = n >= 2 ? context.at(n - 2) + QLatin1Char(' ') + context.at(n - 1) : QString();
        const QString bi = n >= 1 ? context.last() : QString();
        return {NextWordModel(baseData().trigrams, trigrams, tri), NextWordModel(baseData().bigrams, bigrams, bi)};
    }

    void ensureIndex() const
    {
        if (!indexDirty)
            return;
        index.clear();
        index.reserve(size_t(baseData().scores.size() + unigrams.size()));
        for (auto it = baseData().scores.keyBegin(); it != baseData().scores.keyEnd(); ++it)
            index.push_back({stripApostrophes(*it), *it});
        for (auto it = unigrams.keyBegin(); it != unigrams.keyEnd(); ++it) {
            if (!baseData().scores.contains(*it))
                index.push_back({stripApostrophes(*it), *it});
        }
        std::sort(index.begin(), index.end());
        indexDirty = false;
    }

    void addToIndex(const QString &key)
    {
        if (indexDirty || baseData().scores.contains(key))
            return;
        const Entry entry{stripApostrophes(key), key};
        index.insert(std::lower_bound(index.begin(), index.end(), entry), entry);
    }

    const QStringList &topWords() const
    {
        if (!topDirty)
            return top;
        QSet<QString> keys(unigrams.keyBegin(), unigrams.keyEnd());
        const QStringList &base = baseData().top;
        for (qsizetype i = 0; i < qMin<qsizetype>(base.size(), 300); ++i)
            keys.insert(base.at(i));
        std::vector<std::pair<double, QString>> all;
        all.reserve(size_t(keys.size()));
        for (const QString &key : std::as_const(keys))
            all.emplace_back(wordScore(key), key);
        const size_t n = qMin<size_t>(all.size(), 60);
        std::partial_sort(all.begin(), all.begin() + qsizetype(n), all.end(), [](const auto &a, const auto &b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        top.clear();
        for (size_t i = 0; i < n; ++i) {
            if (!hidden.contains(all[i].second))
                top << all[i].second;
        }
        topDirty = false;
        return top;
    }

    void clear()
    {
        unigrams.clear();
        bigrams.clear();
        trigrams.clear();
        forms.clear();
        hidden.clear();
        indexDirty = true;
        topDirty = true;
    }

    void prune()
    {
        bigrams.prune(kMaxNgrams);
        trigrams.prune(kMaxNgrams);
        if (unigrams.size() <= kMaxUnigrams)
            return;
        std::vector<std::pair<int, QString>> items;
        items.reserve(size_t(unigrams.size()));
        for (auto it = unigrams.cbegin(); it != unigrams.cend(); ++it)
            items.emplace_back(it.value(), it.key());
        const size_t drop = items.size() - size_t(kMaxUnigrams) * 9 / 10;
        std::partial_sort(items.begin(), items.begin() + qsizetype(drop), items.end());
        for (size_t i = 0; i < drop; ++i) {
            unigrams.remove(items[i].second);
            forms.remove(items[i].second);
        }
        indexDirty = true;
    }

    void changed()
    {
        topDirty = true;
        dirty = true;
        saveTimer->start();
    }

    QHash<QString, int> unigrams;
    NgramTable bigrams;            // "w1" -> w2
    NgramTable trigrams;           // "w1 w2" -> w3
    QHash<QString, QString> forms; // how the user writes a word mid-sentence ("Sarah")
    QSet<QString> hidden;          // built-in words the user asked not to see

    mutable std::vector<Entry> index;
    mutable bool indexDirty = true;
    mutable QStringList top;
    mutable bool topDirty = true;

    QTimer *saveTimer = nullptr;
    bool dirty = false;
};

WordPredictor::WordPredictor(const QString &storePath, QObject *parent)
    : QObject(parent)
    , d(new Private)
    , m_path(storePath)
{
    d->saveTimer = new QTimer(this);
    d->saveTimer->setSingleShot(true);
    d->saveTimer->setInterval(kSaveDelayMs);
    connect(d->saveTimer, &QTimer::timeout, this, [this] { save(); });
}

WordPredictor::~WordPredictor()
{
    if (d->dirty)
        save();
    delete d;
}

void WordPredictor::load()
{
    d->clear();
    QFile f(m_path);
    if (!m_path.isEmpty() && f.open(QIODevice::ReadOnly)) {
        // {"version":1, "words":["hello 3", "sarah 2 Sarah"], "hidden":["teh"],
        //  "bigrams":["i want 2"], "trigrams":["i want to 2"]}
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonArray words = root.value(QStringLiteral("words")).toArray();
        for (const QJsonValue v : words) {
            const QStringList parts = normalizeApostrophes(v.toString()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const QString key = parts.value(0).toLower();
            const int count = parts.value(1).toInt();
            if (key.isEmpty() || count <= 0)
                continue;
            d->unigrams[key] += count;
            if (parts.size() > 2 && parts.at(2).toLower() == key)
                d->forms.insert(key, parts.at(2));
        }
        const QJsonArray hidden = root.value(QStringLiteral("hidden")).toArray();
        for (const QJsonValue v : hidden)
            d->hidden.insert(v.toString().toLower());
        d->bigrams.fromJson(root.value(QStringLiteral("bigrams")).toArray(), 2);
        d->trigrams.fromJson(root.value(QStringLiteral("trigrams")).toArray(), 3);
        d->prune();
    }
    d->dirty = false;
    emit learnedChanged();
}

void WordPredictor::save() const
{
    d->saveTimer->stop();
    if (m_path.isEmpty())
        return;
    QByteArray json;
    json.reserve(64 + 24 * (d->unigrams.size() + d->bigrams.size + d->trigrams.size));
    json += "{\"version\":1,\"words\":[";
    bool first = true;
    for (auto it = d->unigrams.cbegin(); it != d->unigrams.cend(); ++it) {
        if (!first)
            json += ',';
        first = false;
        QString entry = it.key() + QLatin1Char(' ') + QString::number(it.value());
        const QString form = d->forms.value(it.key());
        if (!form.isEmpty())
            entry += QLatin1Char(' ') + form;
        appendJsonString(json, entry);
    }
    json += "],\"hidden\":[";
    QStringList hidden(d->hidden.cbegin(), d->hidden.cend());
    hidden.sort();
    for (qsizetype i = 0; i < hidden.size(); ++i) {
        if (i > 0)
            json += ',';
        appendJsonString(json, hidden.at(i));
    }
    json += "],\"bigrams\":";
    d->bigrams.appendJson(json);
    json += ",\"trigrams\":";
    d->trigrams.appendJson(json);
    json += "}\n";

    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(json);
    if (f.commit())
        d->dirty = false;
}

void WordPredictor::learn(const QString &sentence)
{
    static const QRegularExpression urls(QStringLiteral("(?:https?://|www\\.)\\S+"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sentenceEnd(QStringLiteral("[.!?\\x{2026}\\n]+"));
    QString text = normalizeApostrophes(sentence);
    text.remove(urls);

    bool learned = false;
    const QStringList sentences = text.split(sentenceEnd, Qt::SkipEmptyParts);
    for (const QString &s : sentences) {
        const QStringList raw = tokens(s);
        QString prev2;
        QString prev1 = startToken();
        for (qsizetype i = 0; i < raw.size(); ++i) {
            const QString &token = raw.at(i);
            if (hasDigit(token) || token.size() > kMaxWordLength) {
                prev2.clear();
                prev1.clear();
                continue;
            }
            const QString key = token.toLower();
            if (!d->unigrams.contains(key))
                d->addToIndex(key);
            ++d->unigrams[key];
            d->hidden.remove(key);
            if (i > 0 && !isAllCaps(token)) {
                // Mid-sentence spelling tells us whether it's a name ("Sarah") or a plain word.
                if (token != key)
                    d->forms.insert(key, token);
                else if (baseData().forms.contains(key))
                    d->forms.insert(key, key);
                else
                    d->forms.remove(key);
            }
            if (!prev1.isEmpty())
                d->bigrams.add(prev1, key);
            if (!prev2.isEmpty())
                d->trigrams.add(prev2 + QLatin1Char(' ') + prev1, key);
            prev2 = prev1;
            prev1 = key;
            learned = true;
        }
    }
    if (!learned)
        return;
    d->prune();
    d->changed();
    emit learnedChanged();
}

QStringList WordPredictor::suggest(const QString &textBeforeCursor, int count) const
{
    if (count <= 0)
        return {};
    const bool truncated = textBeforeCursor.size() > kContextChars;
    const QString text = normalizeApostrophes(truncated ? textBeforeCursor.right(kContextChars) : textBeforeCursor);

    qsizetype start = text.size();
    while (start > 0 && isWordChar(text.at(start - 1)))
        --start;
    while (start < text.size() && isApostrophe(text.at(start)))
        ++start; // an opening quote, not part of the word
    const QString partial = text.mid(start);
    const Context ctx = contextOf(text.left(start), truncated);
    const Private::Predictor next = d->predictor(ctx.words);

    std::vector<std::pair<double, QString>> ranked;
    CaseMode mode = CaseMode::AsIs;
    if (!partial.isEmpty()) {
        // Completions of the word being typed, steered by the words before it.
        const QString prefix = partial.toLower();
        const QString stripped = stripApostrophes(prefix);
        if (stripped.isEmpty() || hasDigit(stripped))
            return {};
        if (isAllCaps(partial))
            mode = CaseMode::Upper;
        else if (partial.at(0).isUpper())
            mode = CaseMode::Capitalize;

        const bool withApostrophe = prefix.contains(QLatin1Char('\''));
        d->ensureIndex();
        auto it = std::lower_bound(d->index.cbegin(), d->index.cend(), Private::Entry{stripped, QString()});
        for (; it != d->index.cend() && it->stripped.startsWith(stripped); ++it) {
            if (it->key == prefix || (withApostrophe && !it->key.startsWith(prefix)) || d->hidden.contains(it->key))
                continue;
            ranked.emplace_back(d->wordScore(it->key) + kContextWeight * next.score(it->key), it->key);
        }
    } else {
        // The next word: trigrams, then bigrams; the most used words fill up the rest.
        if (ctx.sentenceStart)
            mode = CaseMode::Capitalize;
        else if (ctx.shouting)
            mode = CaseMode::Upper;
        QSet<QString> seen;
        const auto consider = [&](const QString &word) {
            if (d->hidden.contains(word) || seen.contains(word))
                return;
            seen.insert(word);
            ranked.emplace_back(next.score(word) * 1000.0 + d->wordScore(word) / 100.0, word);
        };
        next.trigram.forEachWord(consider);
        next.bigram.forEachWord(consider);
    }

    const size_t n = qMin(ranked.size(), size_t(count));
    std::partial_sort(ranked.begin(), ranked.begin() + qsizetype(n), ranked.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    QStringList keys;
    for (size_t i = 0; i < n; ++i)
        keys << ranked[i].second;
    if (partial.isEmpty()) {
        for (const QString &w : d->topWords()) {
            if (keys.size() >= count)
                break;
            if (!keys.contains(w))
                keys << w;
        }
    }

    QStringList out;
    out.reserve(keys.size());
    for (const QString &key : std::as_const(keys))
        out << applyCase(d->display(key), mode);
    return out;
}

QString WordPredictor::applySuggestion(const QString &textBeforeCursor, const QString &word)
{
    const QString &t = textBeforeCursor;
    qsizetype i = t.size();
    while (i > 0 && isWordChar(t.at(i - 1)))
        --i;
    while (i < t.size() && isApostrophe(t.at(i)))
        ++i; // keep an opening quote
    QString before = t.left(i);
    if (i == t.size() && !before.isEmpty()) {
        // A new word: separate it from what came before unless that opens a bracket or quote.
        const QChar last = before.at(before.size() - 1);
        static const QString openers = QStringLiteral(u"([{“‘¿¡");
        const bool quoteOpens = (last == QLatin1Char('"') || isApostrophe(last))
                                && (before.size() == 1 || before.at(before.size() - 2).isSpace());
        if (!last.isSpace() && !openers.contains(last) && !quoteOpens)
            before += QLatin1Char(' ');
    }
    return before + word + QLatin1Char(' ');
}

void WordPredictor::forget(const QString &word)
{
    const QString key = normalizeApostrophes(word).trimmed().toLower();
    if (key.isEmpty())
        return;
    d->unigrams.remove(key);
    d->forms.remove(key);
    if (baseData().scores.contains(key))
        d->hidden.insert(key);
    d->bigrams.removeWord(key);
    d->trigrams.removeWord(key);
    d->indexDirty = true;
    d->changed();
    emit learnedChanged();
}

void WordPredictor::clearLearned()
{
    d->clear();
    d->dirty = true;
    save();
    emit learnedChanged();
}

int WordPredictor::learnedWordCount() const
{
    return int(d->unigrams.size());
}
