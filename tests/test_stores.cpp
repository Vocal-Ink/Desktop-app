#include "core/HistoryModel.h"
#include "core/PhraseStore.h"
#include "core/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestStores : public QObject
{
    Q_OBJECT
private slots:
    void phrasesRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("phrases.json"));
        {
            PhraseStore store(path);
            QVERIFY(store.load()); // defaults when missing
            QVERIFY(!store.phrases().isEmpty());
            store.setPhrases({{QStringLiteral("Hi"), QStringLiteral("Ctrl+Alt+1"), {}},
                              {QStringLiteral("Bye"), {}, QStringLiteral("piper:x")}});
        }
        PhraseStore again(path);
        QVERIFY(again.load());
        QCOMPARE(again.phrases().size(), 2);
        QCOMPARE(again.phrases().at(0).hotkey, QStringLiteral("Ctrl+Alt+1"));
        QCOMPARE(again.phrases().at(1).voiceKey, QStringLiteral("piper:x"));
    }

    void phraseEditing()
    {
        QTemporaryDir dir;
        PhraseStore store(dir.filePath(QStringLiteral("p.json")));
        store.setPhrases({{QStringLiteral("A"), {}, {}}, {QStringLiteral("B"), {}, {}}, {QStringLiteral("C"), {}, {}}});
        QSignalSpy changed(&store, &PhraseStore::changed);
        store.move(0, 2);
        QCOMPARE(store.phrases().at(2).text, QStringLiteral("A"));
        store.removeAt(0);
        QCOMPARE(store.phrases().at(0).text, QStringLiteral("C"));
        store.update(0, {QStringLiteral("Z"), {}, {}});
        QCOMPARE(store.phrases().at(0).text, QStringLiteral("Z"));
        store.removeAt(99); // ignored
        QCOMPARE(changed.size(), 3);
    }

    void corruptPhraseFileIsRejected()
    {
        bool ok = true;
        PhraseStore::fromJson("{nope", &ok);
        QVERIFY(!ok);
    }

    void settingsDefaultsAndChanges()
    {
        QTemporaryDir dir;
        Settings s(dir.filePath(QStringLiteral("s.ini")), nullptr);
        QCOMPARE(s.integer(Keys::ObsPort), 4455);
        QCOMPARE(s.string(Keys::SttMode), QStringLiteral("ptt"));
        QSignalSpy changed(&s, &Settings::changed);
        s.setValue(Keys::ObsPort, 4456);
        s.setValue(Keys::ObsPort, 4456); // unchanged: no second signal
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().at(0).toString(), QString::fromLatin1(Keys::ObsPort));
        QCOMPARE(s.integer(Keys::ObsPort), 4456);
        QVERIFY(s.value(Keys::Replacements).toMap().contains(QStringLiteral("brb")));
    }

    void historyNewestFirst()
    {
        HistoryModel h;
        h.add(1, QStringLiteral("first"), QStringLiteral("V"));
        h.add(2, QStringLiteral("second"), QStringLiteral("V"));
        QCOMPARE(h.rowCount(), 2);
        QCOMPARE(h.lastText(), QStringLiteral("second"));
        h.setStatus(1, HistoryModel::Status::Failed, QStringLiteral("err"));
        QCOMPARE(h.entry(1)->status, HistoryModel::Status::Failed);
        QVERIFY(h.data(h.index(1), Qt::ToolTipRole).toString().contains(QStringLiteral("err")));
        QCOMPARE(h.texts(), (QStringList{QStringLiteral("second"), QStringLiteral("first")}));
    }
};

QTEST_GUILESS_MAIN(TestStores)
#include "test_stores.moc"
