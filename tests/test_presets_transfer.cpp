#include "core/Paths.h"
#include "core/PhraseStore.h"
#include "core/Settings.h"
#include "core/SettingsTransfer.h"
#include "core/VoicePresets.h"
#include "core/WordPredictor.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

VoicePreset makePreset(const QString &name, int rate)
{
    VoicePreset p;
    p.name = name;
    p.voiceKey = QStringLiteral("piper:en_US-lessac-medium");
    p.rate = rate;
    p.pitch = -5;
    p.effect = QStringLiteral("radio");
    p.effectIntensity = 40;
    return p;
}

} // namespace

class TestPresetsTransfer : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() { Paths::setDataDirOverride(QString()); }

    void presetsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("presets.json"));
        VoicePresets presets(path);
        QSignalSpy changed(&presets, &VoicePresets::changed);
        QVERIFY(presets.load()); // no file yet
        QVERIFY(presets.presets().isEmpty());

        const QString chill = presets.add(makePreset(QStringLiteral("Chill stream"), 90));
        const QString work = presets.add(makePreset(QStringLiteral("Work calls"), 110));
        const QString third = presets.add(makePreset(QStringLiteral("Third"), 100));
        QCOMPARE(chill.size(), 8);
        QVERIFY(chill != work && work != third);
        QCOMPARE(presets.preset(work).name, QStringLiteral("Work calls"));
        QVERIFY(presets.preset(QStringLiteral("missing")).id.isEmpty());

        VoicePreset edited = presets.preset(work);
        edited.pitch = 12;
        presets.update(edited);
        QCOMPARE(presets.preset(work).pitch, 12);
        presets.move(2, 0);
        QCOMPARE(presets.presets().first().id, third);
        presets.remove(chill);
        QCOMPARE(presets.presets().size(), 2);
        presets.remove(QStringLiteral("missing"));
        presets.move(0, 5);
        presets.update(presets.preset(work)); // unchanged: no signal
        QCOMPARE(changed.size(), 1 + 3 + 3);

        const QJsonObject root = QJsonDocument::fromJson(readFile(path)).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("presets")).toArray().size(), 2);

        VoicePresets again(path);
        QVERIFY(again.load());
        QCOMPARE(again.presets(), presets.presets());
        QCOMPARE(again.preset(work).effect, QStringLiteral("radio"));
    }

    void presetFilesAreValidated()
    {
        bool ok = true;
        VoicePresets::fromJson("{nope", &ok);
        QVERIFY(!ok);

        // Out-of-range values are clamped; missing or duplicate ids get new ones.
        const QList<VoicePreset> list = VoicePresets::fromJson(
            R"({"version":1,"presets":[{"id":"a","name":"A","rate":900,"pitch":-99},{"name":"B"},{"id":"a","name":"C"}]})",
            &ok);
        QVERIFY(ok);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).rate, 200);
        QCOMPARE(list.at(0).pitch, -50);
        QVERIFY(!list.at(1).id.isEmpty());
        QVERIFY(list.at(2).id != QStringLiteral("a"));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("presets.json"));
        VoicePresets presets(path);
        presets.add(makePreset(QStringLiteral("Keep me"), 100));
        writeFile(path, "garbage");
        QVERIFY(!presets.load());
        QCOMPARE(presets.presets().size(), 1);
    }

    void exportImportRoundTrip()
    {
        QTemporaryDir source;
        Paths::setDataDirOverride(source.path());
        Settings settings(source.filePath(QStringLiteral("settings.ini")), nullptr);
        const QByteArray device("\x01\xff{0.0.0.00000000}", 20);
        const QVariantMap replacements{{QStringLiteral("brb"), QStringLiteral("be right back")},
                                       {QStringLiteral("gtg"), QStringLiteral("got to go")}};
        settings.setValue(Keys::ObsPort, 4460);
        settings.setValue(Keys::Theme, QStringLiteral("light"));
        settings.setValue(Keys::MonitorEnabled, false);
        settings.setValue(Keys::FavoriteVoices, QStringList{QStringLiteral("piper:a"), QStringLiteral("system:b")});
        settings.setValue(Keys::Replacements, replacements);
        settings.setValue(Keys::OutputDevice, device);
        settings.setValue(Keys::WindowGeometry, QByteArray("geometry"));
        settings.setValue(QStringLiteral("keybinds/speak.stop"), QStringLiteral("Ctrl+Alt+Q"));
        settings.setValue(QStringLiteral("obs/password"), QStringLiteral("hunter2"));
        settings.setValue(QStringLiteral("tts/openai/apiKey"), QStringLiteral("sk-secret"));
        settings.sync();

        PhraseStore phrases(Paths::phrasesFile());
        phrases.setPhrases({{QStringLiteral("Hi there"), QStringLiteral("Ctrl+Alt+1"), {}}});
        VoicePresets presets(source.filePath(QStringLiteral("presets.json")));
        const QString presetId = presets.add(makePreset(QStringLiteral("Stream"), 120));
        {
            WordPredictor predictor(source.filePath(QStringLiteral("predictor.json")));
            predictor.learn(QStringLiteral("Zorblax says hello"));
            predictor.save();
        }
        const QByteArray sounds = R"({"version":1,"sounds":[{"id":"x1","name":"Applause","file":"x1.wav"}]})";
        writeFile(source.filePath(QStringLiteral("sounds.json")), sounds);

        const QString backup = source.filePath(QStringLiteral("backup.json"));
        QString error;
        QVERIFY2(SettingsTransfer::exportTo(backup, &settings, &error), qPrintable(error));

        const QJsonObject root = QJsonDocument::fromJson(readFile(backup)).object();
        QCOMPARE(root.value(QStringLiteral("app")).toString(), QStringLiteral("Vocal Ink"));
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        QVERIFY(!root.value(QStringLiteral("exported")).toString().isEmpty());
        const QJsonObject exported = root.value(QStringLiteral("settings")).toObject();
        QCOMPARE(exported.value(QString::fromLatin1(Keys::ObsPort)).toInt(), 4460);
        QVERIFY(exported.value(QString::fromLatin1(Keys::MonitorEnabled)).isBool());
        QVERIFY(!exported.contains(QString::fromLatin1(Keys::WindowGeometry)));
        QVERIFY(!exported.contains(QStringLiteral("obs/password")));
        QVERIFY(!exported.contains(QStringLiteral("tts/openai/apiKey")));
        QVERIFY(!readFile(backup).contains("hunter2"));
        QVERIFY(!readFile(backup).contains("sk-secret"));
        const QJsonObject files = root.value(QStringLiteral("files")).toObject();
        QVERIFY(files.value(QStringLiteral("phrases.json")).isObject());
        QVERIFY(files.value(QStringLiteral("presets.json")).isObject());
        QVERIFY(files.value(QStringLiteral("predictor.json")).isObject());
        QCOMPARE(files.value(QStringLiteral("sounds.json")).toObject(), QJsonDocument::fromJson(sounds).object());

        // Restore into a fresh profile.
        QTemporaryDir target;
        Paths::setDataDirOverride(target.path());
        Settings restored(target.filePath(QStringLiteral("settings.ini")), nullptr);
        QSignalSpy changed(&restored, &Settings::changed);
        QVERIFY2(SettingsTransfer::importFrom(backup, &restored, &error), qPrintable(error));
        QVERIFY(changed.size() >= 6);
        QCOMPARE(restored.integer(Keys::ObsPort), 4460);
        QCOMPARE(restored.string(Keys::Theme), QStringLiteral("light"));
        QCOMPARE(restored.flag(Keys::MonitorEnabled), false);
        QCOMPARE(restored.value(Keys::FavoriteVoices).toStringList(),
                 (QStringList{QStringLiteral("piper:a"), QStringLiteral("system:b")}));
        QCOMPARE(restored.value(Keys::Replacements).toMap(), replacements);
        QCOMPARE(restored.value(Keys::OutputDevice).toByteArray(), device);
        QCOMPARE(restored.value(QStringLiteral("keybinds/speak.stop"), QString()).toString(), QStringLiteral("Ctrl+Alt+Q"));
        QVERIFY(!restored.contains(QString::fromLatin1(Keys::WindowGeometry)));
        QVERIFY(!restored.contains(QStringLiteral("obs/password")));

        PhraseStore restoredPhrases(Paths::phrasesFile());
        QVERIFY(restoredPhrases.load());
        QCOMPARE(restoredPhrases.phrases(), phrases.phrases());
        VoicePresets restoredPresets(target.filePath(QStringLiteral("presets.json")));
        QVERIFY(restoredPresets.load());
        QCOMPARE(restoredPresets.presets(), presets.presets());
        QCOMPARE(restoredPresets.preset(presetId).rate, 120);
        WordPredictor restoredWords(target.filePath(QStringLiteral("predictor.json")));
        restoredWords.load();
        QVERIFY(restoredWords.suggest(QStringLiteral("zorb")).contains(QStringLiteral("zorblax")));
        QCOMPARE(QJsonDocument::fromJson(readFile(target.filePath(QStringLiteral("sounds.json")))).object(),
                 QJsonDocument::fromJson(sounds).object());
    }

    void invalidBackupsChangeNothing()
    {
        QTemporaryDir dir;
        Paths::setDataDirOverride(dir.path());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        settings.setValue(Keys::Theme, QStringLiteral("contrast"));
        const QByteArray phrases = PhraseStore::toJson({{QStringLiteral("Original"), {}, {}}});
        writeFile(Paths::phrasesFile(), phrases);

        const QByteArray validPhrases = PhraseStore::toJson({{QStringLiteral("Replaced"), {}, {}}});
        const QList<QByteArray> invalid = {
            "{not json at all",
            "[1, 2, 3]",
            R"({"app":"Other App","version":1,"settings":{"ui/theme":"dark"},"files":{}})",
            R"({"app":"Vocal Ink","version":2,"settings":{"ui/theme":"dark"},"files":{}})",
            R"({"app":"Vocal Ink","version":0,"settings":{"ui/theme":"dark"},"files":{}})",
            R"({"app":"Vocal Ink","version":1,"settings":"nope","files":{}})",
            R"({"app":"Vocal Ink","version":1,"settings":{"ui/theme":"dark"},"files":[]})",
            // One good file and one broken one: the good one must not be written either.
            R"({"app":"Vocal Ink","version":1,"settings":{"ui/theme":"dark"},"files":{"phrases.json":)" + validPhrases
                + R"(,"presets.json":"broken"}})",
        };
        for (const QByteArray &content : invalid) {
            const QString path = dir.filePath(QStringLiteral("bad.json"));
            writeFile(path, content);
            QString error;
            QVERIFY2(!SettingsTransfer::importFrom(path, &settings, &error), content.constData());
            QVERIFY(!error.isEmpty());
            QCOMPARE(settings.string(Keys::Theme), QStringLiteral("contrast"));
            QCOMPARE(readFile(Paths::phrasesFile()), phrases);
            QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("presets.json"))));
        }

        QString error;
        QVERIFY(!SettingsTransfer::importFrom(dir.filePath(QStringLiteral("missing.json")), &settings, &error));
        QVERIFY(!error.isEmpty());

        // Files that aren't part of a backup are never written, whatever the backup says.
        const QString path = dir.filePath(QStringLiteral("sneaky.json"));
        writeFile(path, R"({"app":"Vocal Ink","version":1,"settings":{},"files":{"../evil.json":{"a":1},"phrases.json":)"
                            + validPhrases + "}}");
        QVERIFY2(SettingsTransfer::importFrom(path, &settings, &error), qPrintable(error));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("../evil.json"))));
        QCOMPARE(readFile(Paths::phrasesFile()), QJsonDocument::fromJson(validPhrases).toJson(QJsonDocument::Indented));
    }
};

QTEST_GUILESS_MAIN(TestPresetsTransfer)
#include "test_presets_transfer.moc"
