#include "core/Paths.h"
#include "models/ModelManager.h"
#include "support/MockHttpServer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

const QString kLessac = QStringLiteral("en_US-lessac-medium");

QByteArray patternBytes(qsizetype size, int seed = 7)
{
    QByteArray b(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        b[i] = char((i * 31 + seed) & 0xff);
    return b;
}

// A file that passes the ggml magic check.
QByteArray fakeWhisperModel(qsizetype size)
{
    QByteArray b = patternBytes(size);
    b.replace(0, 4, QByteArrayLiteral("lmgg"));
    return b;
}

QString md5Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QJsonObject voiceEntry(const QString &key, const QString &name, const QJsonObject &language, const QString &quality,
                       int speakers, const QByteArray &onnx, const QByteArray &config, bool withConfig = true,
                       const QString &configMd5 = QString())
{
    const QString dir = language.value(QLatin1String("family")).toString() + QLatin1Char('/')
                        + language.value(QLatin1String("code")).toString() + QLatin1Char('/') + name + QLatin1Char('/')
                        + quality + QLatin1Char('/');
    QJsonObject files{
        {dir + key + QStringLiteral(".onnx"), QJsonObject{{QStringLiteral("size_bytes"), qint64(onnx.size())},
                                                          {QStringLiteral("md5_digest"), md5Hex(onnx)}}},
        {dir + QStringLiteral("MODEL_CARD"), QJsonObject{{QStringLiteral("size_bytes"), 100},
                                                         {QStringLiteral("md5_digest"), QStringLiteral("00")}}},
    };
    if (withConfig) {
        files.insert(dir + key + QStringLiteral(".onnx.json"),
                     QJsonObject{{QStringLiteral("size_bytes"), qint64(config.size())},
                                 {QStringLiteral("md5_digest"), configMd5.isEmpty() ? md5Hex(config) : configMd5}});
    }
    return QJsonObject{
        {QStringLiteral("key"), key},
        {QStringLiteral("name"), name},
        {QStringLiteral("language"), language},
        {QStringLiteral("quality"), quality},
        {QStringLiteral("num_speakers"), speakers},
        {QStringLiteral("speaker_id_map"), QJsonObject{}},
        {QStringLiteral("files"), files},
        {QStringLiteral("aliases"), QJsonArray{}},
    };
}

QJsonObject language(const char *code, const char *family, const char *english, const char *country)
{
    return QJsonObject{
        {QStringLiteral("code"), QString::fromLatin1(code)},
        {QStringLiteral("family"), QString::fromLatin1(family)},
        {QStringLiteral("name_native"), QString::fromLatin1(english)},
        {QStringLiteral("name_english"), QString::fromLatin1(english)},
        {QStringLiteral("country_english"), QString::fromLatin1(country)},
    };
}

// A small voices.json in the shape of rhasspy/piper-voices.
QByteArray voicesJson(const QByteArray &onnx, const QByteArray &config, const QString &lessacConfigMd5 = QString())
{
    const QJsonObject us = language("en_US", "en", "English", "United States");
    const QJsonObject gb = language("en_GB", "en", "English", "United Kingdom");
    const QJsonObject de = language("de_DE", "de", "German", "Germany");
    QJsonObject root;
    root.insert(QStringLiteral("de_DE-thorsten-high"),
                voiceEntry(QStringLiteral("de_DE-thorsten-high"), QStringLiteral("thorsten"), de, QStringLiteral("high"), 1, onnx, config));
    root.insert(QStringLiteral("en_US-libritts-high"),
                voiceEntry(QStringLiteral("en_US-libritts-high"), QStringLiteral("libritts"), us, QStringLiteral("high"), 904, onnx, config));
    root.insert(kLessac, voiceEntry(kLessac, QStringLiteral("lessac"), us, QStringLiteral("medium"), 1, onnx, config, true, lessacConfigMd5));
    root.insert(QStringLiteral("en_GB-alba-medium"),
                voiceEntry(QStringLiteral("en_GB-alba-medium"), QStringLiteral("alba"), gb, QStringLiteral("medium"), 1, onnx, config));
    root.insert(QStringLiteral("en_US-broken-low"),
                voiceEntry(QStringLiteral("en_US-broken-low"), QStringLiteral("broken"), us, QStringLiteral("low"), 1, onnx, config, false));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

MockHttpServer::Response notFound()
{
    return MockHttpServer::Response::json(R"({"error":"Entry not found"})", 404);
}

struct Rig
{
    QTemporaryDir dir;
    MockHttpServer server;
    QNetworkAccessManager network;
    std::unique_ptr<ModelManager> models;

    Rig()
    {
        Paths::setDataDirOverride(dir.path());
        server.listen();
        models = makeManager();
    }
    std::unique_ptr<ModelManager> makeManager()
    {
        auto m = std::make_unique<ModelManager>(&network);
        m->setWhisperBaseUrl(server.baseUrl() + QStringLiteral("/whisper/"));
        m->setPiperVoicesBaseUrl(server.baseUrl() + QStringLiteral("/voices"));
        return m;
    }
};

QStringList filesIn(const QString &path)
{
    return QDir(path).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestModelManager : public QObject
{
    Q_OBJECT
private slots:
    // --- Whisper ---

    void whisperCatalog()
    {
        const QList<WhisperModelInfo> catalog = ModelManager::whisperCatalog();
        QCOMPARE(catalog.size(), 6);
        int recommended = 0;
        for (const WhisperModelInfo &m : catalog) {
            QVERIFY(m.file.startsWith(QLatin1String("ggml-")) && m.file.endsWith(QLatin1String(".bin")));
            QVERIFY(!m.title.isEmpty() && !m.description.isEmpty());
            QVERIFY(m.approxBytes > 0);
            QCOMPARE(m.multilingual, !m.file.contains(QLatin1String(".en")));
            if (m.recommended) {
                ++recommended;
                QCOMPARE(m.file, QStringLiteral("ggml-base.en-q5_1.bin"));
            }
        }
        QCOMPARE(recommended, 1);
        QCOMPARE(catalog.last().file, QStringLiteral("ggml-large-v3-turbo-q5_0.bin"));
    }

    void downloadsWhisperModel()
    {
        Rig rig;
        const QString file = QStringLiteral("ggml-tiny.en-q5_1.bin");
        const QString taskId = QStringLiteral("whisper:") + file;
        const QByteArray model = fakeWhisperModel(300 * 1024);
        const QString cdn = rig.server.baseUrl() + QStringLiteral("/cdn/blob-1234");
        rig.server.setHandler([model, cdn](const MockHttpServer::Request &req) {
            if (req.path == QLatin1String("/whisper/ggml-tiny.en-q5_1.bin")) {
                // Hugging Face answers with a redirect to its CDN.
                MockHttpServer::Response r = MockHttpServer::Response::bytes("Found", "text/plain", 302);
                r.headers.append({QByteArrayLiteral("Location"), cdn.toUtf8()});
                return r;
            }
            if (req.path != QLatin1String("/cdn/blob-1234"))
                return notFound();
            MockHttpServer::Response r = MockHttpServer::Response::bytes(model, "application/octet-stream");
            r.chunks = 5;
            return r;
        });
        QSignalSpy progress(rig.models.get(), &ModelManager::downloadProgress);
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        QSignalSpy installed(rig.models.get(), &ModelManager::installedChanged);

        QVERIFY(!rig.models->isWhisperModelInstalled(file));
        rig.models->downloadWhisperModel(file);
        rig.models->downloadWhisperModel(file); // already running: ignored
        QVERIFY(rig.models->isDownloading(taskId));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() > 0, 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(finished.first().first().toString(), taskId);
        QVERIFY(!rig.models->isDownloading(taskId));
        QCOMPARE(rig.server.requests().size(), 2); // original + redirect target
        QVERIFY(!rig.server.requests().first().header("user-agent").isEmpty());
        QCOMPARE(installed.count(), 1);

        QVERIFY(rig.models->isWhisperModelInstalled(file));
        QCOMPARE(rig.models->installedWhisperModels(), QStringList{file});
        QCOMPARE(readFile(ModelManager::whisperModelPath(file)), model);
        QCOMPARE(filesIn(Paths::whisperModelsDir()), QStringList{file}); // no temporary files left

        QVERIFY(progress.count() >= 2);
        qint64 last = -1;
        for (const QList<QVariant> &args : std::as_const(progress)) {
            QCOMPARE(args.at(0).toString(), taskId);
            QVERIFY(args.at(1).toLongLong() >= last);
            last = args.at(1).toLongLong();
        }
        QCOMPARE(progress.last().at(1).toLongLong(), qint64(model.size()));
        QCOMPARE(progress.last().at(2).toLongLong(), qint64(model.size()));

        // Removing it.
        QVERIFY(rig.models->removeWhisperModel(file));
        QCOMPARE(installed.count(), 2);
        QVERIFY(!rig.models->isWhisperModelInstalled(file));
        QVERIFY(!rig.models->removeWhisperModel(file));
        QVERIFY(!rig.models->removeWhisperModel(QStringLiteral("../settings.ini")));
    }

    void rejectsFileWithoutGgmlMagic()
    {
        Rig rig;
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::bytes("<!DOCTYPE html><html><body>Sign in to continue</body></html>", "text/html");
        });
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        QSignalSpy installed(rig.models.get(), &ModelManager::installedChanged);
        rig.models->downloadWhisperModel(QStringLiteral("ggml-base.en-q5_1.bin"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("whisper:ggml-base.en-q5_1.bin"));
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("not a Whisper model")));
        QCOMPARE(finished.count(), 0);
        QCOMPARE(installed.count(), 0);
        QVERIFY(filesIn(Paths::whisperModelsDir()).isEmpty());
        QVERIFY(!rig.models->isDownloading(QStringLiteral("whisper:ggml-base.en-q5_1.bin")));
    }

    void reportsHttpErrors()
    {
        Rig rig;
        rig.server.setHandler([](const MockHttpServer::Request &) { return notFound(); });
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        rig.models->downloadWhisperModel(QStringLiteral("ggml-small-q5_1.bin"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!failed.first().at(1).toString().isEmpty());
        QVERIFY(filesIn(Paths::whisperModelsDir()).isEmpty());

        rig.models->downloadWhisperModel(QStringLiteral("../../etc/passwd"));
        QCOMPARE(failed.count(), 2);
        QCOMPARE(rig.server.requests().size(), 1);
    }

    void cancelDiscardsPartialDownload()
    {
        Rig rig;
        const QByteArray model = fakeWhisperModel(3 * 1024 * 1024);
        rig.server.setHandler([model](const MockHttpServer::Request &) {
            MockHttpServer::Response r = MockHttpServer::Response::bytes(model, "application/octet-stream");
            r.chunks = 80; // ~1.2 s in total
            return r;
        });
        const QString taskId = QStringLiteral("whisper:ggml-small.en-q5_1.bin");
        QSignalSpy progress(rig.models.get(), &ModelManager::downloadProgress);
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        rig.models->downloadWhisperModel(QStringLiteral("ggml-small.en-q5_1.bin"));
        const auto receivedSome = [&progress] { return !progress.isEmpty() && progress.last().at(1).toLongLong() > 0; };
        QTRY_VERIFY_WITH_TIMEOUT(receivedSome() || !failed.isEmpty() || !finished.isEmpty(), 15000);
        QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(1).toString()));
        QVERIFY2(finished.isEmpty(), "finished before it could be cancelled");
        QVERIFY2(receivedSome(), qPrintable(QStringLiteral("no data after 15 s (%1 request(s), %2 progress signal(s))")
                                                .arg(rig.server.requests().size())
                                                .arg(progress.size())));
        QVERIFY(progress.last().at(1).toLongLong() < model.size());

        rig.models->cancel(taskId);
        QVERIFY(!rig.models->isDownloading(taskId));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), taskId);
        QTest::qWait(300);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 1);
        QVERIFY(filesIn(Paths::whisperModelsDir()).isEmpty());
    }

    void listsInstalledWhisperModels()
    {
        Rig rig;
        for (const char *name : {"ggml-b.bin", "ggml-a.bin", "notes.txt", "ggml-c.bin.part"}) {
            QFile f(Paths::whisperModelsDir() + QLatin1Char('/') + QString::fromLatin1(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("lmgg");
        }
        QCOMPARE(rig.models->installedWhisperModels(), (QStringList{QStringLiteral("ggml-a.bin"), QStringLiteral("ggml-b.bin")}));
        QVERIFY(rig.models->isWhisperModelInstalled(QStringLiteral("ggml-a.bin")));
        QVERIFY(!rig.models->isWhisperModelInstalled(QStringLiteral("ggml-d.bin")));
        QCOMPARE(ModelManager::whisperModelPath(QStringLiteral("ggml-a.bin")),
                 Paths::whisperModelsDir() + QStringLiteral("/ggml-a.bin"));
    }

    // --- Piper voices ---

    void parsesAndCachesPiperCatalog()
    {
        Rig rig;
        const QByteArray onnx = patternBytes(1000);
        const QByteArray config = QByteArrayLiteral(R"({"audio":{"sample_rate":22050}})");
        const QByteArray json = voicesJson(onnx, config);
        rig.server.setHandler([json](const MockHttpServer::Request &req) {
            return req.path == QLatin1String("/voices/voices.json") ? MockHttpServer::Response::json(json) : notFound();
        });
        QSignalSpy changed(rig.models.get(), &ModelManager::piperCatalogChanged);
        QSignalSpy errors(rig.models.get(), &ModelManager::piperCatalogError);
        QVERIFY(rig.models->piperCatalog().isEmpty());
        rig.models->refreshPiperCatalog();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 15000);
        QCOMPARE(errors.count(), 0);

        const QList<PiperVoiceInfo> voices = rig.models->piperCatalog();
        QStringList keys;
        for (const PiperVoiceInfo &v : voices)
            keys << v.key;
        // Sorted by language, then name; the voice without a config file is left out.
        QCOMPARE(keys, (QStringList{QStringLiteral("en_GB-alba-medium"), kLessac, QStringLiteral("en_US-libritts-high"),
                                    QStringLiteral("de_DE-thorsten-high")}));
        const PiperVoiceInfo &lessac = voices.at(1);
        QCOMPARE(lessac.name, QStringLiteral("lessac"));
        QCOMPARE(lessac.languageCode, QStringLiteral("en_US"));
        QCOMPARE(lessac.languageName, QStringLiteral("English (United States)"));
        QCOMPARE(lessac.quality, QStringLiteral("medium"));
        QCOMPARE(lessac.numSpeakers, 1);
        QCOMPARE(lessac.files.size(), 2);
        QCOMPARE(lessac.files.at(0).relPath, QStringLiteral("en/en_US/lessac/medium/en_US-lessac-medium.onnx"));
        QCOMPARE(lessac.files.at(1).relPath, QStringLiteral("en/en_US/lessac/medium/en_US-lessac-medium.onnx.json"));
        QCOMPARE(lessac.files.at(0).md5, md5Hex(onnx));
        QCOMPARE(lessac.totalBytes(), qint64(onnx.size() + config.size()));
        QCOMPARE(voices.at(2).numSpeakers, 904);
        QCOMPARE(voices.at(3).languageName, QStringLiteral("German (Germany)"));
        QVERIFY(QFileInfo::exists(Paths::dataDir() + QStringLiteral("/piper-voices.json")));

        // A new manager starts from the saved list, even when the server is unreachable.
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json("{}", 503);
        });
        std::unique_ptr<ModelManager> offline = rig.makeManager();
        QCOMPARE(offline->piperCatalog().size(), 4);
        QSignalSpy offlineErrors(offline.get(), &ModelManager::piperCatalogError);
        QSignalSpy offlineChanged(offline.get(), &ModelManager::piperCatalogChanged);
        offline->refreshPiperCatalog();
        QTRY_COMPARE_WITH_TIMEOUT(offlineErrors.count(), 1, 15000);
        QCOMPARE(offlineChanged.count(), 0);
        QCOMPARE(offline->piperCatalog().size(), 4);
    }

    void downloadsPiperVoiceWithChecksums()
    {
        Rig rig;
        const QByteArray onnx = patternBytes(200 * 1024);
        const QByteArray config = QByteArrayLiteral(R"({"audio":{"sample_rate":22050},"num_speakers":1})");
        const QByteArray json = voicesJson(onnx, config);
        rig.server.setHandler([json, onnx, config](const MockHttpServer::Request &req) {
            if (req.path == QLatin1String("/voices/voices.json"))
                return MockHttpServer::Response::json(json);
            if (req.path.endsWith(QLatin1String(".onnx"))) {
                MockHttpServer::Response r = MockHttpServer::Response::bytes(onnx, "application/octet-stream");
                r.chunks = 3;
                return r;
            }
            if (req.path.endsWith(QLatin1String(".onnx.json")))
                return MockHttpServer::Response::json(config);
            return notFound();
        });
        QSignalSpy changed(rig.models.get(), &ModelManager::piperCatalogChanged);
        rig.models->refreshPiperCatalog();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 15000);

        const QString taskId = QStringLiteral("piper-voice:") + kLessac;
        QSignalSpy progress(rig.models.get(), &ModelManager::downloadProgress);
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        QSignalSpy installed(rig.models.get(), &ModelManager::installedChanged);
        QVERIFY(!rig.models->isPiperVoiceInstalled(kLessac));
        rig.models->downloadPiperVoice(kLessac);
        QVERIFY(rig.models->isDownloading(taskId));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() > 0, 15000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(finished.first().first().toString(), taskId);
        QCOMPARE(installed.count(), 1);

        const QString model = ModelManager::piperVoiceModelPath(kLessac);
        QCOMPARE(model, Paths::piperVoicesDir() + QStringLiteral("/en_US-lessac-medium/en_US-lessac-medium.onnx"));
        QVERIFY(rig.models->isPiperVoiceInstalled(kLessac));
        QCOMPARE(readFile(model), onnx);
        QCOMPARE(readFile(model + QStringLiteral(".json")), config);
        QCOMPARE(filesIn(QFileInfo(model).absolutePath()).size(), 2);

        QStringList paths;
        for (const MockHttpServer::Request &req : rig.server.requests())
            paths << req.path;
        QVERIFY(paths.contains(QStringLiteral("/voices/en/en_US/lessac/medium/en_US-lessac-medium.onnx")));
        QVERIFY(paths.contains(QStringLiteral("/voices/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json")));

        // Progress covers both files.
        const qint64 total = onnx.size() + config.size();
        qint64 last = -1;
        for (const QList<QVariant> &args : std::as_const(progress)) {
            QCOMPARE(args.at(0).toString(), taskId);
            QCOMPARE(args.at(2).toLongLong(), total);
            QVERIFY(args.at(1).toLongLong() >= last);
            last = args.at(1).toLongLong();
        }
        QCOMPARE(last, total);

        QVERIFY(rig.models->removePiperVoice(kLessac));
        QCOMPARE(installed.count(), 2);
        QVERIFY(!rig.models->isPiperVoiceInstalled(kLessac));
        QVERIFY(!QFileInfo::exists(QFileInfo(model).absolutePath()));
    }

    void rejectsPiperVoiceWithBadChecksum()
    {
        Rig rig;
        const QByteArray onnx = patternBytes(50 * 1024);
        const QByteArray config = QByteArrayLiteral(R"({"audio":{"sample_rate":22050}})");
        const QByteArray json = voicesJson(onnx, config, QStringLiteral("0123456789abcdef0123456789abcdef"));
        rig.server.setHandler([json, onnx, config](const MockHttpServer::Request &req) {
            if (req.path == QLatin1String("/voices/voices.json"))
                return MockHttpServer::Response::json(json);
            if (req.path.endsWith(QLatin1String(".onnx")))
                return MockHttpServer::Response::bytes(onnx, "application/octet-stream");
            if (req.path.endsWith(QLatin1String(".onnx.json")))
                return MockHttpServer::Response::json(config);
            return notFound();
        });
        QSignalSpy changed(rig.models.get(), &ModelManager::piperCatalogChanged);
        rig.models->refreshPiperCatalog();
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 15000);

        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        rig.models->downloadPiperVoice(kLessac);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("checksum")));
        QCOMPARE(finished.count(), 0);
        QVERIFY(!rig.models->isPiperVoiceInstalled(kLessac));
        const QString model = ModelManager::piperVoiceModelPath(kLessac);
        QVERIFY(!QFileInfo::exists(model)); // the good file is not kept either
        QVERIFY(!QFileInfo::exists(QFileInfo(model).absolutePath()));

        // Voices that are not in the list cannot be downloaded.
        rig.models->downloadPiperVoice(QStringLiteral("xx_XX-nobody-low"));
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QStringLiteral("piper-voice:xx_XX-nobody-low"));
    }

    // --- Piper runtime ---

    void runtimeUrlMatchesPlatform()
    {
        const QUrl url = ModelManager::piperRuntimeUrl();
        if (url.isEmpty())
            QSKIP("No Piper build for this platform");
        QVERIFY(url.toString().startsWith(QLatin1String("https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_")));
#if defined(Q_OS_WIN)
        QCOMPARE(url.fileName(), QStringLiteral("piper_windows_amd64.zip"));
#elif defined(Q_OS_LINUX)
        if (QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64"))
            QCOMPARE(url.fileName(), QStringLiteral("piper_linux_x86_64.tar.gz"));
#endif
        QVERIFY(ModelManager::bundledPiperExecutable().startsWith(Paths::piperRuntimeDir() + QStringLiteral("/piper/piper")));
    }

    void downloadsAndUnpacksPiperRuntime()
    {
        const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
        if (tar.isEmpty())
            QSKIP("tar is not available");
        Rig rig;

        // Build a miniature release archive with the same layout as the real one.
        QTemporaryDir src;
        const QString exeName = QFileInfo(ModelManager::bundledPiperExecutable()).fileName();
        QVERIFY(QDir(src.path()).mkpath(QStringLiteral("piper/espeak-ng-data")));
        {
            QFile exe(src.filePath(QStringLiteral("piper/") + exeName));
            QVERIFY(exe.open(QIODevice::WriteOnly));
            exe.write("#!/bin/sh\necho piper\n");
            exe.close();
            exe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QFile data(src.filePath(QStringLiteral("piper/espeak-ng-data/phontab")));
            QVERIFY(data.open(QIODevice::WriteOnly));
            data.write("phonemes");
        }
        QProcess pack;
        pack.setWorkingDirectory(src.path());
        pack.start(tar, {QStringLiteral("-czf"), QStringLiteral("runtime.tar.gz"), QStringLiteral("piper")});
        if (!pack.waitForFinished(30000) || pack.exitStatus() != QProcess::NormalExit || pack.exitCode() != 0)
            QSKIP("Could not create a test archive with tar");
        const QByteArray archive = readFile(src.filePath(QStringLiteral("runtime.tar.gz")));
        QVERIFY(!archive.isEmpty());

        rig.server.setHandler([archive](const MockHttpServer::Request &req) {
            if (req.path != QLatin1String("/runtime/piper_test.tar.gz"))
                return notFound();
            MockHttpServer::Response r = MockHttpServer::Response::bytes(archive, "application/gzip");
            r.chunks = 2;
            return r;
        });
        rig.models->setPiperRuntimeUrlOverride(QUrl(rig.server.baseUrl() + QStringLiteral("/runtime/piper_test.tar.gz")));
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        QSignalSpy installed(rig.models.get(), &ModelManager::installedChanged);

        QVERIFY(!rig.models->isPiperRuntimeInstalled());
        rig.models->downloadPiperRuntime();
        QVERIFY(rig.models->isDownloading(QStringLiteral("piper-runtime")));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() > 0, 60000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(finished.first().first().toString(), QStringLiteral("piper-runtime"));
        QCOMPARE(installed.count(), 1);

        QVERIFY(rig.models->isPiperRuntimeInstalled());
        const QString exe = ModelManager::bundledPiperExecutable();
        QCOMPARE(readFile(exe), QByteArray("#!/bin/sh\necho piper\n"));
#ifndef Q_OS_WIN
        QVERIFY(QFileInfo(exe).isExecutable());
#endif
        QVERIFY(QFileInfo::exists(Paths::piperRuntimeDir() + QStringLiteral("/piper/espeak-ng-data/phontab")));
        QVERIFY(!QFileInfo::exists(Paths::piperRuntimeDir() + QStringLiteral("/.unpack")));
        QVERIFY(filesIn(Paths::dataDir() + QStringLiteral("/downloads")).isEmpty());

        // Installing again replaces the previous copy.
        rig.models->downloadPiperRuntime();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() > 1, 60000);
        QCOMPARE(failed.count(), 0);
        QVERIFY(rig.models->isPiperRuntimeInstalled());
    }

    void failsOnBrokenRuntimeArchive()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("tar")).isEmpty())
            QSKIP("tar is not available");
        Rig rig;
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::bytes("this is not an archive at all", "application/gzip");
        });
        rig.models->setPiperRuntimeUrlOverride(QUrl(rig.server.baseUrl() + QStringLiteral("/runtime/piper.tar.gz")));
        QSignalSpy finished(rig.models.get(), &ModelManager::downloadFinished);
        QSignalSpy failed(rig.models.get(), &ModelManager::downloadFailed);
        rig.models->downloadPiperRuntime();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 60000);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("piper-runtime"));
        QCOMPARE(finished.count(), 0);
        QVERIFY(!rig.models->isPiperRuntimeInstalled());
        QVERIFY(!QFileInfo::exists(Paths::piperRuntimeDir() + QStringLiteral("/.unpack")));
        QVERIFY(filesIn(Paths::dataDir() + QStringLiteral("/downloads")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestModelManager)
#include "test_modelmanager.moc"
