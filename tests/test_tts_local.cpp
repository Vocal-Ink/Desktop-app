#include "audio/AudioConvert.h"
#include "core/Paths.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "tts/Engines.h"
#include "tts/EspeakTtsEngine.h"
#include "tts/PiperTtsEngine.h"
#include "tts/SystemTtsEngine.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

namespace {

struct StreamResult
{
    QByteArray pcm;
    QAudioFormat format;
    QString error;
    bool finished = false;
};

StreamResult collect(TtsStream *stream, int timeoutMs = 10000)
{
    StreamResult r;
    QEventLoop loop;
    QObject::connect(stream, &TtsStream::audioReady, stream, [&r](const QAudioFormat &format, const QByteArray &pcm) {
        r.format = format;
        r.pcm += pcm;
    });
    QObject::connect(stream, &TtsStream::finished, &loop, [&] {
        r.finished = true;
        loop.quit();
    });
    QObject::connect(stream, &TtsStream::failed, &loop, [&](const QString &error) {
        r.error = error;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    delete stream;
    return r;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray piperConfig(int sampleRate, const QString &code, const QString &dataset, const QJsonObject &speakers)
{
    const QJsonObject o{
        {QStringLiteral("audio"), QJsonObject{{QStringLiteral("sample_rate"), sampleRate}, {QStringLiteral("quality"), QStringLiteral("medium")}}},
        {QStringLiteral("espeak"), QJsonObject{{QStringLiteral("voice"), QStringLiteral("en-us")}}},
        {QStringLiteral("inference"), QJsonObject{{QStringLiteral("noise_scale"), 0.667}, {QStringLiteral("length_scale"), 1}, {QStringLiteral("noise_w"), 0.8}}},
        {QStringLiteral("phoneme_type"), QStringLiteral("espeak")},
        {QStringLiteral("num_symbols"), 256},
        {QStringLiteral("num_speakers"), speakers.isEmpty() ? 1 : int(speakers.size())},
        {QStringLiteral("speaker_id_map"), speakers},
        {QStringLiteral("piper_version"), QStringLiteral("1.0.0")},
        {QStringLiteral("language"), QJsonObject{{QStringLiteral("code"), code},
                                                 {QStringLiteral("name_english"), QStringLiteral("English")},
                                                 {QStringLiteral("country_english"), QStringLiteral("United States")}}},
        {QStringLiteral("dataset"), dataset},
    };
    return QJsonDocument(o).toJson();
}

Voice findVoice(const QList<Voice> &voices, const QString &id)
{
    for (const Voice &v : voices) {
        if (v.id == id)
            return v;
    }
    return {};
}

} // namespace

class TestTtsLocal : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // Keep engines that look at the data directory away from the real one.
        QVERIFY(m_dataDir.isValid());
        Paths::setDataDirOverride(m_dataDir.path());
    }

    void cleanupTestCase()
    {
        Paths::setDataDirOverride(QString());
    }

    void factoryOrder()
    {
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("s.ini")), nullptr);
        SecretStore secrets(SecretStore::Backend::Memory);
        QNetworkAccessManager network;
        QObject parent;
        const QList<TtsEngine *> engines = createTtsEngines(EngineContext{&network, &settings, &secrets}, &parent);
        QStringList ids;
        for (TtsEngine *e : engines) {
            ids << e->id();
            QCOMPARE(e->parent(), &parent);
            QVERIFY(!e->displayName().isEmpty());
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("piper"), QStringLiteral("system"), QStringLiteral("espeak"),
                                   QStringLiteral("azure"), QStringLiteral("elevenlabs"), QStringLiteral("fishaudio"),
                                   QStringLiteral("openai")}));
        QVERIFY(engines.at(0)->isLocal() && engines.at(1)->isLocal() && engines.at(2)->isLocal());
        for (int i = 3; i < engines.size(); ++i) {
            QVERIFY(!engines.at(i)->isLocal());
            QVERIFY(!engines.at(i)->isAvailable()); // no keys yet
        }
    }

    // --- eSpeak NG ---------------------------------------------------------------

    void espeakParsesVoiceTable()
    {
        const QByteArray table =
            "Pty Language       Age/Gender VoiceName          File                 Other Languages\n"
            " 5  af              --/M      Afrikaans          gmw/af               \n"
            " 2  en-gb           --/M      English_(Great_Britain) gmw/en               (en 2)\n"
            " 5  en-gb-x-rp      --/M      English_(Received_Pronunciation) gmw/en-GB-x-rp       (en-gb 4)(en 5)\r\n"
            " 2  en-us           --/F      English_(America)  gmw/en-US            (en 3)\n"
            " 5  en-029          --/M      English_(Caribbean) gmw/en-029           (en 10)\n"
            "\n";
        const QList<Voice> voices = EspeakTtsEngine::parseVoiceList(table);
        QCOMPARE(voices.size(), 5);
        const Voice us = findVoice(voices, QStringLiteral("en-us"));
        QCOMPARE(us.engineId, QStringLiteral("espeak"));
        QCOMPARE(us.name, QStringLiteral("English (America)"));
        QCOMPARE(us.language, QStringLiteral("en-US"));
        QCOMPARE(us.gender, QStringLiteral("Female"));
        QCOMPARE(findVoice(voices, QStringLiteral("en-gb-x-rp")).language, QStringLiteral("en-GB-x-rp"));
        QCOMPARE(findVoice(voices, QStringLiteral("en-029")).language, QStringLiteral("en-029"));
        QCOMPARE(findVoice(voices, QStringLiteral("af")).gender, QStringLiteral("Male"));
    }

    void espeakListsAndSpeaks()
    {
        if (EspeakTtsEngine::executablePath().isEmpty())
            QSKIP("espeak-ng is not installed");
        EspeakTtsEngine engine(EngineContext{});
        QVERIFY(engine.isAvailable());
        QVERIFY(engine.isLocal());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QVERIFY(changed.wait(10000));

        Voice english;
        for (const Voice &v : engine.voices()) {
            if (v.id == QLatin1String("en-us") || (!english.isValid() && v.id.startsWith(QLatin1String("en"))))
                english = v;
        }
        QVERIFY2(english.isValid(), "no English eSpeak voice listed");

        SpeakOptions options;
        options.rate = 1.2;
        options.pitch = 0.3;
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hello from Vocal Ink. This is a test."), english, options));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QVERIFY(r.format.isValid());
        QVERIFY(r.format.sampleRate() >= 8000);
        QCOMPARE(r.format.channelCount(), 1);
        const double seconds = double(r.pcm.size()) / r.format.bytesPerFrame() / r.format.sampleRate();
        QVERIFY2(seconds > 0.5 && seconds < 20.0, qPrintable(QString::number(seconds)));
        const QVector<float> mono = AudioConvert::toMonoFloat(r.pcm, r.format);
        QVERIFY(AudioConvert::peak(mono.constData(), mono.size()) > 0.05f); // not silence
    }

    void espeakCancel()
    {
        if (EspeakTtsEngine::executablePath().isEmpty())
            QSKIP("espeak-ng is not installed");
        EspeakTtsEngine engine(EngineContext{});
        Voice v;
        v.engineId = engine.id();
        v.id = QStringLiteral("en");
        TtsStream *stream = engine.synthesize(QStringLiteral("This sentence is cancelled before it starts."), v, SpeakOptions());
        QSignalSpy audio(stream, &TtsStream::audioReady);
        QSignalSpy done(stream, &TtsStream::finished);
        QSignalSpy failed(stream, &TtsStream::failed);
        stream->cancel();
        QTest::qWait(300);
        QCOMPARE(audio.size(), 0);
        QCOMPARE(done.size(), 0);
        QCOMPARE(failed.size(), 0);
        delete stream;
    }

    // --- Piper -------------------------------------------------------------------

    void piperScansVoices()
    {
        QTemporaryDir data;
        Paths::setDataDirOverride(data.path());
        const QString voices = Paths::piperVoicesDir();
        const QJsonObject speakers{{QStringLiteral("alice"), 0}, {QStringLiteral("bob"), 1}};
        QVERIFY(writeFile(voices + QStringLiteral("/en_US-test-medium/en_US-test-medium.onnx"), "onnx"));
        QVERIFY(writeFile(voices + QStringLiteral("/en_US-test-medium/en_US-test-medium.onnx.json"),
                          piperConfig(22050, QStringLiteral("en_US"), QStringLiteral("test"), speakers)));
        QVERIFY(writeFile(voices + QStringLiteral("/de_DE-thorsten-low.onnx"), "onnx"));
        QVERIFY(writeFile(voices + QStringLiteral("/de_DE-thorsten-low.onnx.json"),
                          piperConfig(16000, QStringLiteral("de_DE"), QStringLiteral("thorsten"), {})));
        // Ignored: no config next to the model, and too deep.
        QVERIFY(writeFile(voices + QStringLiteral("/fr_FR-noconfig-low/fr_FR-noconfig-low.onnx"), "onnx"));
        QVERIFY(writeFile(voices + QStringLiteral("/a/b/it_IT-deep-low.onnx"), "onnx"));
        QVERIFY(writeFile(voices + QStringLiteral("/a/b/it_IT-deep-low.onnx.json"),
                          piperConfig(22050, QStringLiteral("it_IT"), QStringLiteral("deep"), {})));

        QTemporaryDir settingsDir;
        Settings settings(settingsDir.filePath(QStringLiteral("s.ini")), nullptr);
        PiperTtsEngine engine(EngineContext{nullptr, &settings, nullptr});
        QCOMPARE(engine.id(), QStringLiteral("piper"));
        QVERIFY(engine.isLocal());

        const QList<Voice> list = engine.voices();
        QStringList ids;
        for (const Voice &v : list)
            ids << v.id;
        QCOMPARE(ids, (QStringList{QStringLiteral("de_DE-thorsten-low"), QStringLiteral("en_US-test-medium#alice"),
                                   QStringLiteral("en_US-test-medium#bob")}));
        const Voice bob = list.at(2);
        QCOMPARE(bob.engineId, QStringLiteral("piper"));
        QCOMPARE(bob.speaker, 1);
        QCOMPARE(bob.language, QStringLiteral("en-US"));
        QVERIFY(bob.name.startsWith(QLatin1String("Test (medium)")));
        QVERIFY(bob.name.contains(QLatin1String("bob")));
        QCOMPARE(bob.description, QStringLiteral("English (United States)"));
        QCOMPARE(list.at(0).speaker, -1);
        QCOMPARE(list.at(0).language, QStringLiteral("de-DE"));
        QCOMPARE(list.at(0).name, QStringLiteral("Thorsten (medium)"));

        if (QStandardPaths::findExecutable(QStringLiteral("piper")).isEmpty()) {
            QVERIFY(engine.executablePath().isEmpty());
            QVERIFY(!engine.isAvailable());
            QVERIFY(engine.unavailableReason().contains(QLatin1String("engine")));
        }

        // New voices show up on refresh.
        QVERIFY(writeFile(voices + QStringLiteral("/en_GB-new-low/en_GB-new-low.onnx"), "onnx"));
        QVERIFY(writeFile(voices + QStringLiteral("/en_GB-new-low/en_GB-new-low.onnx.json"),
                          piperConfig(16000, QStringLiteral("en_GB"), QStringLiteral("new"), {})));
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(engine.voices().size(), 4);
        Paths::setDataDirOverride(m_dataDir.path());
    }

    void piperRunsExecutable()
    {
#ifdef Q_OS_WIN
        QSKIP("the stand-in piper is a shell script");
#else
        QTemporaryDir data;
        Paths::setDataDirOverride(data.path());
        const QString voices = Paths::piperVoicesDir();
        const QJsonObject speakers{{QStringLiteral("alice"), 0}, {QStringLiteral("bob"), 1}};
        const QString onnx = voices + QStringLiteral("/en_US-test-medium/en_US-test-medium.onnx");
        QVERIFY(writeFile(onnx, "onnx"));
        QVERIFY(writeFile(onnx + QStringLiteral(".json"), piperConfig(22050, QStringLiteral("en_US"), QStringLiteral("test"), speakers)));
        const QString single = voices + QStringLiteral("/de_DE-thorsten-low/de_DE-thorsten-low.onnx");
        QVERIFY(writeFile(single, "onnx"));
        QVERIFY(writeFile(single + QStringLiteral(".json"), piperConfig(16000, QStringLiteral("de_DE"), QStringLiteral("thorsten"), {})));

        // Records how it was called, then "speaks" the PCM in pcm.bin.
        const QString toolDir = data.filePath(QStringLiteral("tools"));
        const QString script = toolDir + QStringLiteral("/fake-piper");
        QVERIFY(writeFile(script,
                          "#!/bin/sh\n"
                          "dir=$(cd \"$(dirname \"$0\")\" && pwd -P)\n"
                          ": > \"$dir/args.txt\"\n"
                          "for a in \"$@\"; do printf '%s\\n' \"$a\" >> \"$dir/args.txt\"; done\n"
                          "pwd -P > \"$dir/cwd.txt\"\n"
                          "cat > \"$dir/stdin.txt\"\n"
                          "cat \"$dir/pcm.bin\"\n"));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QByteArray pcm(22050, Qt::Uninitialized); // 0.5 s
        for (int i = 0; i < pcm.size() / 2; ++i)
            qToLittleEndian<qint16>(qint16(i * 11 % 30000 - 15000), pcm.data() + 2 * i);
        QVERIFY(writeFile(toolDir + QStringLiteral("/pcm.bin"), pcm));

        QTemporaryDir settingsDir;
        Settings settings(settingsDir.filePath(QStringLiteral("s.ini")), nullptr);
        settings.setValue(Keys::PiperExecutable, script);
        PiperTtsEngine engine(EngineContext{nullptr, &settings, nullptr});
        QCOMPARE(engine.executablePath(), QFileInfo(script).absoluteFilePath());
        QVERIFY(engine.isAvailable());
        QVERIFY(engine.unavailableReason().isEmpty());

        SpeakOptions fast;
        fast.rate = 2.0;
        const Voice bob = findVoice(engine.voices(), QStringLiteral("en_US-test-medium#bob"));
        QVERIFY(bob.isValid());
        StreamResult r = collect(engine.synthesize(QStringLiteral("Hello world,\nsecond line"), bob, fast));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QCOMPARE(r.pcm, pcm);
        QCOMPARE(r.format.sampleRate(), 22050);
        QCOMPARE(r.format.channelCount(), 1);
        QCOMPARE(r.format.sampleFormat(), QAudioFormat::Int16);
        const QStringList expected = {
            QStringLiteral("--model"), QFileInfo(onnx).absoluteFilePath(),
            QStringLiteral("--config"), QFileInfo(onnx).absoluteFilePath() + QStringLiteral(".json"),
            QStringLiteral("--output_raw"),
            QStringLiteral("--quiet"),
            QStringLiteral("--speaker"), QStringLiteral("1"),
            QStringLiteral("--length_scale"), QStringLiteral("0.500"),
        };
        QCOMPARE(QString::fromUtf8(readFile(toolDir + QStringLiteral("/args.txt"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts), expected);
        QCOMPARE(readFile(toolDir + QStringLiteral("/stdin.txt")), QByteArray("Hello world, second line\n"));
        QCOMPARE(QFileInfo(QString::fromUtf8(readFile(toolDir + QStringLiteral("/cwd.txt")).trimmed())).canonicalFilePath(),
                 QFileInfo(toolDir).canonicalFilePath());

        // A voice restored from its key finds its speaker by name.
        r = collect(engine.synthesize(QStringLiteral("Hi"), Voice::fromKey(QStringLiteral("piper:en_US-test-medium#alice")), SpeakOptions()));
        QVERIFY(r.finished);
        QStringList args = QString::fromUtf8(readFile(toolDir + QStringLiteral("/args.txt"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(args.at(args.indexOf(QStringLiteral("--speaker")) + 1), QStringLiteral("0"));
        QCOMPARE(args.last(), QStringLiteral("1.000"));

        // Single-speaker model: no --speaker, its own sample rate.
        r = collect(engine.synthesize(QStringLiteral("Hallo"), Voice::fromKey(QStringLiteral("piper:de_DE-thorsten-low")), SpeakOptions()));
        QVERIFY(r.finished);
        QCOMPARE(r.format.sampleRate(), 16000);
        args = QString::fromUtf8(readFile(toolDir + QStringLiteral("/args.txt"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(!args.contains(QStringLiteral("--speaker")));

        // Unknown voice.
        r = collect(engine.synthesize(QStringLiteral("Hi"), Voice::fromKey(QStringLiteral("piper:xx_XX-missing-low")), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY(r.error.contains(QLatin1String("xx_XX-missing-low")));

        // Without the setting, the runtime the app downloads is used.
        settings.setValue(Keys::PiperExecutable, QString());
        const QString bundled = Paths::piperRuntimeDir() + QStringLiteral("/piper/piper");
        QVERIFY(QDir().mkpath(QFileInfo(bundled).absolutePath()));
        QVERIFY(QFile::copy(script, bundled));
        QCOMPARE(engine.executablePath(), bundled);
        Paths::setDataDirOverride(m_dataDir.path());
#endif
    }

    void piperFailingProcess()
    {
#ifdef Q_OS_WIN
        QSKIP("the stand-in piper is a shell script");
#else
        QTemporaryDir data;
        Paths::setDataDirOverride(data.path());
        const QString onnx = Paths::piperVoicesDir() + QStringLiteral("/en_US-test-medium/en_US-test-medium.onnx");
        QVERIFY(writeFile(onnx, "onnx"));
        QVERIFY(writeFile(onnx + QStringLiteral(".json"), piperConfig(22050, QStringLiteral("en_US"), QStringLiteral("test"), {})));
        const QString script = data.filePath(QStringLiteral("broken-piper"));
        QVERIFY(writeFile(script, "#!/bin/sh\ncat > /dev/null\necho 'Unable to load model' >&2\nexit 3\n"));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        QTemporaryDir settingsDir;
        Settings settings(settingsDir.filePath(QStringLiteral("s.ini")), nullptr);
        settings.setValue(Keys::PiperExecutable, script);
        PiperTtsEngine engine(EngineContext{nullptr, &settings, nullptr});
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), engine.voices().first(), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY2(r.error.contains(QLatin1String("Unable to load model")), qPrintable(r.error));
        Paths::setDataDirOverride(m_dataDir.path());
#endif
    }

    // --- System voices -------------------------------------------------------------

    void systemEngine()
    {
        SystemTtsEngine engine(EngineContext{});
        QCOMPARE(engine.id(), QStringLiteral("system"));
        QVERIFY(engine.isLocal());
        QVERIFY(!engine.displayName().isEmpty());
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        QVERIFY(!engine.isAvailable());
        QVERIFY(engine.unavailableReason().contains(QLatin1String("6.6")));
        Voice v;
        v.engineId = engine.id();
        v.id = QStringLiteral("sapi|Zira|en_US");
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), v, SpeakOptions()), 2000);
        QVERIFY(!r.finished);
        QVERIFY(!r.error.isEmpty());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QCOMPARE(changed.size(), 1);
        QVERIFY(engine.voices().isEmpty());
#endif
    }

private:
    QTemporaryDir m_dataDir;
};

QTEST_GUILESS_MAIN(TestTtsLocal)
#include "test_tts_local.moc"
