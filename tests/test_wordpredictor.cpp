#include "core/WordPredictor.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestWordPredictor : public QObject
{
    Q_OBJECT
private slots:
    void completesFromBuiltInList()
    {
        QTemporaryDir dir;
        WordPredictor p(dir.filePath(QStringLiteral("predictor.json")));
        const QStringList th = p.suggest(QStringLiteral("th"));
        QCOMPARE(th.size(), 5);
        QVERIFY(th.contains(QStringLiteral("the")));
        QVERIFY(th.contains(QStringLiteral("that")));

        // The exact typed word is not offered, longer words are.
        const QStringList the = p.suggest(QStringLiteral("I like the"));
        QVERIFY(!the.contains(QStringLiteral("the")));
        QVERIFY(the.contains(QStringLiteral("them")) || the.contains(QStringLiteral("then"))
                || the.contains(QStringLiteral("there")));

        QVERIFY(p.suggest(QStringLiteral("xqzv")).isEmpty());
        QVERIFY(p.suggest(QStringLiteral("abc"), 0).isEmpty());
        QCOMPARE(p.suggest(QStringLiteral("a"), 3).size(), 3);
    }

    void apostrophesAndContractions()
    {
        WordPredictor p(QString{});
        QVERIFY(p.suggest(QStringLiteral("don")).contains(QStringLiteral("don't")));
        QVERIFY(p.suggest(QStringLiteral("dont")).contains(QStringLiteral("don't")));
        QCOMPARE(p.suggest(QStringLiteral("don'")).value(0), QStringLiteral("don't"));
        QCOMPARE(p.suggest(QStringLiteral(u"don\u2019")).value(0), QStringLiteral("don't"));
        QVERIFY(p.suggest(QStringLiteral("so im")).contains(QStringLiteral("I'm")));
    }

    void contextSteersCompletions()
    {
        WordPredictor p(QString{});
        QCOMPARE(p.suggest(QStringLiteral("thank y")).value(0), QStringLiteral("you"));
        QCOMPARE(p.suggest(QStringLiteral("I want t")).value(0), QStringLiteral("to"));
    }

    void nextWordsFromBuiltInPairs()
    {
        WordPredictor p(QString{});
        QCOMPARE(p.suggest(QStringLiteral("thank ")).value(0), QStringLiteral("you"));
        // Sentence starts are capitalised; "I" always is.
        const QStringList start = p.suggest(QString());
        QCOMPARE(start.size(), 5);
        QCOMPARE(start.value(0), QStringLiteral("I"));
        for (const QString &w : start)
            QVERIFY2(w.at(0).isUpper(), qPrintable(w));
        const QStringList afterStop = p.suggest(QStringLiteral("Hello there. "));
        QCOMPARE(afterStop, start);
        QVERIFY(p.suggest(QStringLiteral("where is ")).size() == 5);
    }

    void learnsNextWords()
    {
        QTemporaryDir dir;
        WordPredictor p(dir.filePath(QStringLiteral("predictor.json")));
        QSignalSpy changed(&p, &WordPredictor::learnedChanged);
        p.learn(QStringLiteral("I want to play Minecraft with Sarah."));
        p.learn(QStringLiteral("Let's play Minecraft later!"));
        QCOMPARE(changed.size(), 2);
        QVERIFY(p.learnedWordCount() >= 8);

        // Learned words keep how they were written mid-sentence.
        QCOMPARE(p.suggest(QStringLiteral("we could play ")).value(0), QStringLiteral("Minecraft"));
        p.learn(QStringLiteral("I talked to Sarah today. Sarah says hi."));
        QVERIFY(p.suggest(QStringLiteral("call sar")).contains(QStringLiteral("Sarah")));
        // Learned words outrank built-in ones with the same prefix.
        for (int i = 0; i < 3; ++i)
            p.learn(QStringLiteral("minecraft is my favorite"));
        QCOMPARE(p.suggest(QStringLiteral("mi")).value(0), QStringLiteral("Minecraft"));
        QCOMPARE(p.suggest(QStringLiteral("Mi")).value(0), QStringLiteral("Minecraft"));
        p.learn(QStringLiteral("I play minecraft a lot"));
        QCOMPARE(p.suggest(QStringLiteral("mi")).value(0), QStringLiteral("minecraft"));
        QCOMPARE(p.suggest(QStringLiteral("MI")).value(0), QStringLiteral("MINECRAFT"));
    }

    void trigramsBeatBigrams()
    {
        WordPredictor p(QString{});
        p.learn(QStringLiteral("we like dogs. we like dogs. they like dogs."));
        p.learn(QStringLiteral("I like cats."));
        QCOMPARE(p.suggest(QStringLiteral("I like ")).value(0), QStringLiteral("cats"));
        QCOMPARE(p.suggest(QStringLiteral("we like ")).value(0), QStringLiteral("dogs"));
        // Unknown two-word context: back off to what follows "like".
        QCOMPARE(p.suggest(QStringLiteral("zebras like ")).value(0), QStringLiteral("dogs"));
        // Unknown word: back off to the most used words.
        const QStringList fallback = p.suggest(QStringLiteral("zzyzx "));
        QCOMPARE(fallback.size(), 5);
        QVERIFY(fallback.contains(QStringLiteral("like")));
    }

    void persistsLearnedData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("predictor.json"));
        {
            WordPredictor p(path);
            p.learn(QStringLiteral("Gloria makes the best quesadillas."));
            p.learn(QStringLiteral("I love quesadillas so much."));
            p.save();
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        QVERIFY(root.value(QStringLiteral("words")).toArray().contains(QStringLiteral("quesadillas 2")));
        QVERIFY(root.value(QStringLiteral("bigrams")).toArray().contains(QStringLiteral("best quesadillas 1")));
        QVERIFY(root.value(QStringLiteral("trigrams")).toArray().contains(QStringLiteral("the best quesadillas 1")));
        f.close();

        WordPredictor again(path);
        QSignalSpy changed(&again, &WordPredictor::learnedChanged);
        again.load();
        QCOMPARE(changed.size(), 1);
        QVERIFY(again.learnedWordCount() > 0);
        QVERIFY(again.suggest(QStringLiteral("the ques")).contains(QStringLiteral("quesadillas")));
        QCOMPARE(again.suggest(QStringLiteral("the best ")).value(0), QStringLiteral("quesadillas"));

        again.clearLearned();
        QCOMPARE(again.learnedWordCount(), 0);
        QVERIFY(!again.suggest(QStringLiteral("ques")).contains(QStringLiteral("quesadillas")));
        WordPredictor cleared(path);
        cleared.load();
        QCOMPARE(cleared.learnedWordCount(), 0);
    }

    void savesAutomaticallyAfterLearning()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("predictor.json"));
        {
            WordPredictor p(path);
            p.learn(QStringLiteral("zorblax is here"));
        } // unsaved changes are written on destruction
        WordPredictor again(path);
        again.load();
        QVERIFY(again.suggest(QStringLiteral("zorb")).contains(QStringLiteral("zorblax")));
    }

    void corruptStoreIsIgnored()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("predictor.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        WordPredictor p(path);
        p.load();
        QCOMPARE(p.learnedWordCount(), 0);
        QVERIFY(!p.suggest(QStringLiteral("th")).isEmpty());
    }

    void forgetHidesAWord()
    {
        WordPredictor p(QString{});
        p.learn(QStringLiteral("I said teh thing. teh end. teh."));
        QVERIFY(p.suggest(QStringLiteral("te")).contains(QStringLiteral("teh")));
        p.forget(QStringLiteral("teh"));
        QVERIFY(!p.suggest(QStringLiteral("te")).contains(QStringLiteral("teh")));
        QVERIFY(p.suggest(QStringLiteral("the")).contains(QStringLiteral("there")));
        p.forget(QStringLiteral("there"));
        QVERIFY(!p.suggest(QStringLiteral("the")).contains(QStringLiteral("there")));
        p.learn(QStringLiteral("over there"));
        QVERIFY(p.suggest(QStringLiteral("the")).contains(QStringLiteral("there")));
    }

    void caseFollowsInput()
    {
        WordPredictor p(QString{});
        QCOMPARE(p.suggest(QStringLiteral("Th")).value(0), QStringLiteral("The"));
        QCOMPARE(p.suggest(QStringLiteral("TH")).value(0), QStringLiteral("THE"));
        QVERIFY(p.suggest(QStringLiteral("see you on mon")).contains(QStringLiteral("Monday")));
        QCOMPARE(p.suggest(QStringLiteral("so i'")).value(0), QStringLiteral("I'm"));
        QVERIFY(p.suggest(QStringLiteral("I")).contains(QStringLiteral("In")));
        const QStringList shouting = p.suggest(QStringLiteral("I AM SO "));
        for (const QString &w : shouting)
            QCOMPARE(w, w.toUpper());
        const QStringList ok = p.suggest(QStringLiteral("thank "));
        QCOMPARE(ok.value(0), QStringLiteral("you"));
    }

    void appliesSuggestions()
    {
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("hel"), QStringLiteral("hello")), QStringLiteral("hello "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("I said hel"), QStringLiteral("hello")),
                 QStringLiteral("I said hello "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("I "), QStringLiteral("want")), QStringLiteral("I want "));
        QCOMPARE(WordPredictor::applySuggestion(QString(), QStringLiteral("Hi")), QStringLiteral("Hi "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("I don'"), QStringLiteral("don't")),
                 QStringLiteral("I don't "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral(u"I don\u2019"), QStringLiteral("don't")),
                 QStringLiteral("I don't "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("Hello,"), QStringLiteral("how")),
                 QStringLiteral("Hello, how "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("Hi."), QStringLiteral("How")), QStringLiteral("Hi. How "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("he said 'ye"), QStringLiteral("yes")),
                 QStringLiteral("he said 'yes "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("(so"), QStringLiteral("sorry")),
                 QStringLiteral("(sorry "));
        QCOMPARE(WordPredictor::applySuggestion(QStringLiteral("a ("), QStringLiteral("maybe")),
                 QStringLiteral("a (maybe "));
    }

    void isFast()
    {
        WordPredictor p(QString{});
        for (int i = 0; i < 200; ++i)
            p.learn(QStringLiteral("message number word%1 about stuff and things").arg(QChar(QLatin1Char('a' + i % 26))));
        p.suggest(QStringLiteral("s")); // builds the index
        const QStringList inputs = {QStringLiteral("s"), QStringLiteral("I want to "), QStringLiteral("th"),
                                    QStringLiteral("Hello. "), QStringLiteral("can you tell me ab")};
        QElapsedTimer timer;
        timer.start();
        int calls = 0;
        for (int round = 0; round < 40; ++round) {
            for (const QString &in : inputs) {
                p.suggest(in);
                ++calls;
            }
        }
        const double perCall = double(timer.nsecsElapsed()) / 1e6 / calls;
        qInfo("suggest(): %.3f ms per call", perCall);
        QVERIFY2(perCall < 2.0, qPrintable(QString::number(perCall)));
    }
};

QTEST_GUILESS_MAIN(TestWordPredictor)
#include "test_wordpredictor.moc"
