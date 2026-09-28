#include "core/TextProcessor.h"

#include <QTest>

class TestTextProcessor : public QObject
{
    Q_OBJECT
private slots:
    void expandsWholeWordsOnly()
    {
        const QVariantMap map{{QStringLiteral("brb"), QStringLiteral("be right back")},
                              {QStringLiteral("gg"), QStringLiteral("good game")}};
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("ok brb!"), map), QStringLiteral("ok be right back!"));
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("eggs gg"), map), QStringLiteral("eggs good game"));
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("brbx"), map), QStringLiteral("brbx"));
    }

    void keepsCapitalisation()
    {
        const QVariantMap map{{QStringLiteral("gg"), QStringLiteral("good game")}};
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("GG everyone"), map), QStringLiteral("Good game everyone"));
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("Gg"), map), QStringLiteral("Good game"));
    }

    void emptyMapIsIdentity()
    {
        QCOMPARE(TextProcessor::expandReplacements(QStringLiteral("hello"), {}), QStringLiteral("hello"));
    }

    void shortTextIsOneChunk()
    {
        const QStringList chunks = TextProcessor::splitForSpeech(QStringLiteral("Hello there. How are you?"));
        QCOMPARE(chunks.size(), 1);
    }

    void longTextSplitsAtSentences()
    {
        const QString text = QStringLiteral(
            "This is the first sentence and it is reasonably long. "
            "Here comes a second sentence that is also fairly long! "
            "And a third one that asks a question about the weather today?");
        const QStringList chunks = TextProcessor::splitForSpeech(text, 40, 300);
        QCOMPARE(chunks.size(), 3);
        QVERIFY(chunks.at(0).endsWith(QLatin1Char('.')));
        QVERIFY(chunks.at(1).endsWith(QLatin1Char('!')));
        QCOMPARE(chunks.join(QLatin1Char(' ')), text);
    }

    void tinySentencesAreMerged()
    {
        const QString text = QStringLiteral(
            "Hi. Ok. This sentence is long enough to be its own chunk of speech output. Yes. No. Another long sentence follows here right now.");
        const QStringList chunks = TextProcessor::splitForSpeech(text, 40, 300);
        for (const QString &c : chunks)
            QVERIFY2(c.size() >= 10, qPrintable(c));
        QCOMPARE(chunks.join(QLatin1Char(' ')), text);
    }

    void overlongSentenceIsBroken()
    {
        QString text;
        for (int i = 0; i < 60; ++i)
            text += QStringLiteral("word%1, ").arg(i);
        const QStringList chunks = TextProcessor::splitForSpeech(text, 40, 120);
        QVERIFY(chunks.size() > 3);
        for (const QString &c : chunks)
            QVERIFY(c.size() <= 120);
    }

    void cleansTranscripts()
    {
        QCOMPARE(TextProcessor::cleanTranscript(QStringLiteral(" [BLANK_AUDIO] ")), QString());
        QCOMPARE(TextProcessor::cleanTranscript(QStringLiteral("(music) hello  there [laughs]")), QStringLiteral("hello there"));
        QCOMPARE(TextProcessor::cleanTranscript(QStringLiteral("♪♪")), QString());
        QCOMPARE(TextProcessor::cleanTranscript(QStringLiteral(" - ")), QString());
        QCOMPARE(TextProcessor::cleanTranscript(QStringLiteral(" Hello world. ")), QStringLiteral("Hello world."));
    }

    void normalizesWhitespace()
    {
        QCOMPARE(TextProcessor::normalizeForSpeech(QStringLiteral("a\n\tb   c")), QStringLiteral("a b c"));
        QCOMPARE(TextProcessor::normalizeForSpeech(QStringLiteral("  ...  ")), QString());
    }

    void vocabulary()
    {
        const QStringList words = TextProcessor::vocabularyFrom(QStringLiteral("I don't know, Pizza is GREAT"));
        QVERIFY(words.contains(QStringLiteral("don't")));
        QVERIFY(words.contains(QStringLiteral("pizza")));
        QVERIFY(words.contains(QStringLiteral("great")));
        QVERIFY(!words.contains(QStringLiteral("is")));
    }
};

QTEST_GUILESS_MAIN(TestTextProcessor)
#include "test_textprocessor.moc"
