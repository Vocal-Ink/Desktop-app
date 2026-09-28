#include "core/Languages.h"

#include <QDir>
#include <QFile>
#include <QtTest>

class TestLanguages : public QObject
{
    Q_OBJECT
private slots:
    void matchesTheSystemLanguage_data()
    {
        QTest::addColumn<QStringList>("uiLanguages");
        QTest::addColumn<QString>("expected");
        QTest::newRow("german austria") << QStringList{"de-AT", "de", "en-US"} << "de";
        QTest::newRow("brazil") << QStringList{"pt-BR"} << "pt_BR";
        QTest::newRow("portugal falls back to brazil") << QStringList{"pt-PT"} << "pt_BR";
        QTest::newRow("simplified chinese") << QStringList{"zh-Hans-CN", "zh-CN", "zh"} << "zh_CN";
        QTest::newRow("singapore") << QStringList{"zh-SG"} << "zh_CN";
        QTest::newRow("traditional is skipped") << QStringList{"zh-Hant-TW", "zh-TW", "ja-JP"} << "ja";
        QTest::newRow("hong kong alone") << QStringList{"zh-HK"} << "en";
        QTest::newRow("unsupported then french") << QStringList{"sv-SE", "fr-CA"} << "fr";
        QTest::newRow("underscores") << QStringList{"ko_KR"} << "ko";
        QTest::newRow("nothing") << QStringList{} << "en";
        QTest::newRow("turkish") << QStringList{"tr-TR"} << "tr";
    }
    void matchesTheSystemLanguage()
    {
        QFETCH(QStringList, uiLanguages);
        QFETCH(QString, expected);
        QCOMPARE(Languages::match(uiLanguages), expected);
    }

    void everyLanguageHasAFileAndAName()
    {
        // tests/data may not exist, so resolve ".." by hand.
        const QString dir = QDir::cleanPath(qEnvironmentVariable("VOCALINK_TEST_DATA") + QStringLiteral("/../../i18n"));
        const QStringList codes = Languages::supported();
        QCOMPARE(codes.size(), 12);
        for (const QString &code : codes) {
            QVERIFY2(QFile::exists(dir + QStringLiteral("/vocalink_") + code + QStringLiteral(".ts")), qPrintable(code));
            QVERIFY(!Languages::nativeName(code).isEmpty());
            QVERIFY(Languages::nativeName(code) != code);
            // What the system reports for this locale leads back to it.
            QCOMPARE(Languages::match(QLocale(code).uiLanguages()), code);
        }
    }
};

QTEST_GUILESS_MAIN(TestLanguages)
#include "test_languages.moc"
