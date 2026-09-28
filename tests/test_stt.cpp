#include "audio/AudioConvert.h"
#include "core/Paths.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "stt/OpenAiSttEngine.h"
#include "stt/SttController.h"
#include "stt/SttEngines.h"
#include "stt/WhisperEngine.h"
#include "support/MockHttpServer.h"
#include "support/TestUtil.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

QVector<float> noise(int rate, double seconds, float amplitude)
{
    QVector<float> out(qsizetype(rate * seconds));
    auto *rng = QRandomGenerator::global();
    for (float &s : out)
        s = amplitude * float(rng->generateDouble() * 2.0 - 1.0);
    return out;
}

// Recogniser that answers every request with `reply` after a short delay.
class FakeSttEngine : public SttEngine
{
public:
    explicit FakeSttEngine(QObject *parent = nullptr)
        : SttEngine(parent)
    {
    }
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    bool isLocal() const override { return true; }
    bool isReady() const override { return ready; }
    QString notReadyReason() const override { return ready ? QString() : QStringLiteral("The fake model is missing"); }

    void transcribe(quint64 requestId, const QVector<float> &mono16k) override
    {
        requests << Request{requestId, mono16k};
        if (!autoReply)
            return;
        const QString text = reply;
        QTimer::singleShot(5, this, [this, requestId, text] { emit transcribed(requestId, text); });
    }

    struct Request
    {
        quint64 id = 0;
        QVector<float> audio;
    };
    QList<Request> requests;
    bool ready = true;
    bool autoReply = true;
    QString reply = QStringLiteral("hello there");
};

struct Rig
{
    FakeSttEngine engine;
    SttController controller;
    Rig()
    {
        controller.setSimulatedInputForTesting(true);
        controller.setEngine(&engine);
    }
    void feed(const QVector<float> &audio, qsizetype block = 1600)
    {
        for (qsizetype pos = 0; pos < audio.size(); pos += block)
            controller.feedForTesting(audio.mid(pos, block));
    }
    void record(double seconds)
    {
        controller.startListening();
        feed(TestUtil::sine(220, 16000, seconds, 0.3f));
        controller.stopListening();
    }
};

QList<bool> boolArgs(const QSignalSpy &spy)
{
    QList<bool> out;
    for (const QList<QVariant> &args : spy)
        out << args.at(0).toBool();
    return out;
}

// --- multipart/form-data parsing for the cloud engine tests ---
struct FormPart
{
    QByteArray name;
    QByteArray filename;
    QByteArray contentType;
    QByteArray body;
};

QByteArray quotedParam(const QByteArray &header, const QByteArray &param)
{
    const QByteArray key = param + "=\"";
    const qsizetype start = header.indexOf(key);
    if (start < 0)
        return {};
    const qsizetype from = start + key.size();
    return header.mid(from, header.indexOf('"', from) - from);
}

QList<FormPart> parseMultipart(const QByteArray &contentType, const QByteArray &body)
{
    QList<FormPart> parts;
    const qsizetype b = contentType.indexOf("boundary=");
    if (b < 0)
        return parts;
    QByteArray boundary = contentType.mid(b + 9).trimmed();
    if (boundary.startsWith('"'))
        boundary = boundary.mid(1, boundary.size() - 2);
    const QByteArray delimiter = "--" + boundary;
    qsizetype pos = body.indexOf(delimiter);
    while (pos >= 0) {
        qsizetype start = pos + delimiter.size();
        if (body.mid(start, 2) == "--")
            break;
        start += 2; // CRLF
        const qsizetype next = body.indexOf(delimiter, start);
        if (next < 0)
            break;
        const QByteArray chunk = body.mid(start, next - start - 2);
        const qsizetype headerEnd = chunk.indexOf("\r\n\r\n");
        FormPart part;
        part.body = chunk.mid(headerEnd + 4);
        const QList<QByteArray> lines = chunk.left(headerEnd).split('\n');
        for (const QByteArray &raw : lines) {
            const QByteArray line = raw.trimmed();
            const QByteArray lower = line.toLower();
            if (lower.startsWith("content-disposition:")) {
                part.name = quotedParam(line, "name");
                part.filename = quotedParam(line, "filename");
            } else if (lower.startsWith("content-type:")) {
                part.contentType = line.mid(13).trimmed();
            }
        }
        parts << part;
        pos = next;
    }
    return parts;
}

const FormPart *findPart(const QList<FormPart> &parts, const char *name)
{
    for (const FormPart &p : parts) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

struct CloudRig
{
    QTemporaryDir dir;
    MockHttpServer server;
    QNetworkAccessManager network;
    Settings settings{dir.filePath(QStringLiteral("settings.ini")), nullptr};
    SecretStore secrets{SecretStore::Backend::Memory};
    std::unique_ptr<OpenAiSttEngine> engine;

    CloudRig()
    {
        server.listen();
        settings.setValue(Keys::SttOpenAiBaseUrl, QString(server.baseUrl() + QStringLiteral("/v1/")));
        engine = std::make_unique<OpenAiSttEngine>(EngineContext{&network, &settings, &secrets});
    }
};

} // namespace

class TestStt : public QObject
{
    Q_OBJECT
private slots:
    // --- SttController ---

    void pushToTalkTranscribesRecording()
    {
        Rig rig;
        QSignalSpy listening(&rig.controller, &SttController::listeningChanged);
        QSignalSpy active(&rig.controller, &SttController::speechActiveChanged);
        QSignalSpy busy(&rig.controller, &SttController::busyChanged);
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        QSignalSpy errors(&rig.controller, &SttController::errorOccurred);

        rig.controller.startListening();
        QVERIFY(rig.controller.isListening());
        rig.feed(TestUtil::sine(220, 16000, 1.0, 0.3f));
        QVERIFY(rig.engine.requests.isEmpty());
        rig.controller.stopListening();
        QVERIFY(!rig.controller.isListening());

        QCOMPARE(rig.engine.requests.size(), 1);
        QCOMPARE(rig.engine.requests.first().audio.size(), qsizetype(16000));
        QVERIFY(rig.controller.isBusy());
        QTRY_COMPARE(transcripts.count(), 1);
        QCOMPARE(transcripts.first().first().toString(), QStringLiteral("hello there"));
        QVERIFY(!rig.controller.isBusy());
        QCOMPARE(boolArgs(busy), (QList<bool>{true, false}));
        QCOMPARE(boolArgs(listening), (QList<bool>{true, false}));
        QCOMPARE(boolArgs(active), (QList<bool>{true, false}));
        QCOMPARE(errors.count(), 0);
    }

    void shortRecordingsAreIgnored()
    {
        Rig rig;
        QSignalSpy errors(&rig.controller, &SttController::errorOccurred);
        QSignalSpy busy(&rig.controller, &SttController::busyChanged);
        rig.record(0.2);
        QVERIFY(rig.engine.requests.isEmpty());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(busy.count(), 0);
        rig.record(0.35);
        QCOMPARE(rig.engine.requests.size(), 1);
    }

    void longRecordingsAreSentInPieces()
    {
        Rig rig;
        rig.engine.autoReply = false;
        rig.controller.startListening();
        const QVector<float> second = TestUtil::sine(220, 16000, 1.0, 0.3f);
        for (int i = 0; i < 61; ++i)
            rig.controller.feedForTesting(second);
        QCOMPARE(rig.engine.requests.size(), 1);
        QCOMPARE(rig.engine.requests.at(0).audio.size(), qsizetype(16000) * SttController::MaxRecordingSeconds);
        QVERIFY(rig.controller.isListening());
        rig.controller.stopListening();
        QCOMPARE(rig.engine.requests.size(), 2);
        QCOMPARE(rig.engine.requests.at(1).audio.size(), qsizetype(16000));
    }

    void toggleStartsAndStops()
    {
        Rig rig;
        rig.controller.setMode(SttController::Mode::Toggle);
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        rig.controller.toggleListening();
        QVERIFY(rig.controller.isListening());
        rig.feed(TestUtil::sine(220, 16000, 0.5, 0.3f));
        rig.controller.toggleListening();
        QVERIFY(!rig.controller.isListening());
        QCOMPARE(rig.engine.requests.size(), 1);
        QTRY_COMPARE(transcripts.count(), 1);
    }

    void handsFreeTranscribesEachUtterance()
    {
        Rig rig;
        rig.controller.setMode(SttController::Mode::HandsFree);
        rig.engine.reply = QStringLiteral("utterance");
        QSignalSpy active(&rig.controller, &SttController::speechActiveChanged);
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);

        rig.controller.startListening();
        QVERIFY(rig.controller.isListening());
        QVector<float> signal = noise(16000, 1.0, 0.002f);
        signal += TestUtil::sine(220, 16000, 1.0, 0.3f);
        signal += noise(16000, 1.5, 0.002f);
        signal += TestUtil::sine(330, 16000, 0.8, 0.3f);
        signal += noise(16000, 1.5, 0.002f);
        rig.feed(signal, 1234);

        QCOMPARE(rig.engine.requests.size(), 2);
        for (const FakeSttEngine::Request &r : std::as_const(rig.engine.requests)) {
            const double seconds = r.audio.size() / 16000.0;
            QVERIFY2(seconds > 0.7 && seconds < 1.7, qPrintable(QString::number(seconds)));
        }
        QVERIFY(rig.engine.requests.at(0).id != rig.engine.requests.at(1).id);
        QVERIFY(rig.controller.isListening());
        QCOMPARE(boolArgs(active), (QList<bool>{true, false, true, false}));
        QTRY_COMPARE(transcripts.count(), 2);

        rig.controller.stopListening();
        QCOMPARE(rig.engine.requests.size(), 2);
    }

    void handsFreeStopFlushesSpeechInProgress()
    {
        Rig rig;
        rig.controller.setMode(SttController::Mode::HandsFree);
        QSignalSpy active(&rig.controller, &SttController::speechActiveChanged);
        rig.controller.startListening();
        QVector<float> signal = noise(16000, 0.5, 0.002f);
        signal += TestUtil::sine(220, 16000, 0.8, 0.3f);
        rig.feed(signal);
        QVERIFY(rig.engine.requests.isEmpty());
        rig.controller.stopListening();
        QCOMPARE(rig.engine.requests.size(), 1);
        QCOMPARE(boolArgs(active), (QList<bool>{true, false}));

        // Cancelling hands-free drops the utterance in progress.
        rig.controller.startListening();
        rig.feed(signal);
        rig.controller.cancel();
        QCOMPARE(rig.engine.requests.size(), 1);
        QCOMPARE(boolArgs(active), (QList<bool>{true, false, true, false}));
    }

    void resultsAreMatchedToRequests()
    {
        Rig rig;
        rig.engine.autoReply = false;
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        QSignalSpy errors(&rig.controller, &SttController::errorOccurred);
        QSignalSpy busy(&rig.controller, &SttController::busyChanged);

        rig.record(0.5);
        rig.record(0.5);
        QCOMPARE(rig.engine.requests.size(), 2);
        const quint64 first = rig.engine.requests.at(0).id;
        const quint64 second = rig.engine.requests.at(1).id;
        QVERIFY(first != second);

        emit rig.engine.transcribed(first + second + 100, QStringLiteral("stray"));
        emit rig.engine.transcribed(second, QStringLiteral("second"));
        QVERIFY(rig.controller.isBusy());
        emit rig.engine.transcribed(second, QStringLiteral("again")); // already answered
        emit rig.engine.failed(first, QStringLiteral("boom"));

        QCOMPARE(transcripts.count(), 1);
        QCOMPARE(transcripts.first().first().toString(), QStringLiteral("second"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), QStringLiteral("boom"));
        QVERIFY(!rig.controller.isBusy());
        QCOMPARE(boolArgs(busy), (QList<bool>{true, false}));
    }

    void resultsFromPreviousEngineAreIgnored()
    {
        Rig rig;
        rig.engine.autoReply = false;
        FakeSttEngine other;
        other.autoReply = false;
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        QSignalSpy busy(&rig.controller, &SttController::busyChanged);

        rig.record(0.5);
        QVERIFY(rig.controller.isBusy());
        const quint64 old = rig.engine.requests.first().id;
        rig.controller.setEngine(&other);
        QCOMPARE(rig.controller.engine(), &other);
        QVERIFY(!rig.controller.isBusy());
        emit rig.engine.transcribed(old, QStringLiteral("late"));
        QCOMPARE(transcripts.count(), 0);

        rig.record(0.5);
        QCOMPARE(other.requests.size(), 1);
        emit other.transcribed(other.requests.first().id, QStringLiteral("fresh"));
        QCOMPARE(transcripts.count(), 1);
        QCOMPARE(transcripts.first().first().toString(), QStringLiteral("fresh"));
        QCOMPARE(boolArgs(busy), (QList<bool>{true, false, true, false}));
        rig.controller.setEngine(nullptr);
    }

    void destroyedEngineClearsBusyState()
    {
        SttController controller;
        controller.setSimulatedInputForTesting(true);
        auto *engine = new FakeSttEngine;
        engine->autoReply = false;
        controller.setEngine(engine);
        controller.startListening();
        controller.feedForTesting(TestUtil::sine(220, 16000, 0.5, 0.3f));
        controller.stopListening();
        QVERIFY(controller.isBusy());
        delete engine;
        QVERIFY(!controller.engine());
        QVERIFY(!controller.isBusy());
    }

    void notReadyEngineReportsError()
    {
        Rig rig;
        rig.engine.ready = false;
        QSignalSpy errors(&rig.controller, &SttController::errorOccurred);
        QSignalSpy listening(&rig.controller, &SttController::listeningChanged);
        rig.controller.startListening();
        QVERIFY(!rig.controller.isListening());
        QCOMPARE(listening.count(), 0);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), QStringLiteral("The fake model is missing"));

        // The engine goes away while recording.
        rig.engine.ready = true;
        rig.controller.startListening();
        rig.feed(TestUtil::sine(220, 16000, 1.0, 0.3f));
        rig.engine.ready = false;
        rig.controller.stopListening();
        QCOMPARE(errors.count(), 2);
        QVERIFY(rig.engine.requests.isEmpty());
        QVERIFY(!rig.controller.isBusy());

        SttController bare;
        bare.setSimulatedInputForTesting(true);
        QSignalSpy bareErrors(&bare, &SttController::errorOccurred);
        bare.startListening();
        QVERIFY(!bare.isListening());
        QCOMPARE(bareErrors.count(), 1);
        QVERIFY(!bareErrors.first().first().toString().isEmpty());
    }

    void cancelDiscardsRecordingAndPendingResults()
    {
        Rig rig;
        rig.engine.autoReply = false;
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        QSignalSpy active(&rig.controller, &SttController::speechActiveChanged);

        rig.controller.startListening();
        rig.feed(TestUtil::sine(220, 16000, 1.0, 0.3f));
        rig.controller.cancel();
        QVERIFY(!rig.controller.isListening());
        QVERIFY(rig.engine.requests.isEmpty());
        QCOMPARE(boolArgs(active), (QList<bool>{true, false}));

        rig.record(1.0);
        QVERIFY(rig.controller.isBusy());
        rig.controller.cancel();
        QVERIFY(!rig.controller.isBusy());
        emit rig.engine.transcribed(rig.engine.requests.first().id, QStringLiteral("too late"));
        QCOMPARE(transcripts.count(), 0);
    }

    void changingModeStopsListening()
    {
        Rig rig;
        rig.controller.startListening();
        rig.feed(TestUtil::sine(220, 16000, 1.0, 0.3f));
        rig.controller.setMode(SttController::Mode::HandsFree);
        QVERIFY(!rig.controller.isListening());
        QVERIFY(rig.engine.requests.isEmpty());
        QCOMPARE(rig.controller.mode(), SttController::Mode::HandsFree);
    }

    void transcriptsAreCleaned()
    {
        Rig rig;
        QSignalSpy transcripts(&rig.controller, &SttController::transcript);
        rig.engine.reply = QStringLiteral("[BLANK_AUDIO]");
        rig.record(0.5);
        QTRY_VERIFY(!rig.controller.isBusy());
        QCOMPARE(transcripts.count(), 0);

        rig.engine.reply = QStringLiteral("  (music) hello   world ");
        rig.record(0.5);
        QTRY_COMPARE(transcripts.count(), 1);
        QCOMPARE(transcripts.first().first().toString(), QStringLiteral("hello world"));
    }

    void modeStrings()
    {
        QCOMPARE(SttController::modeFromString(QStringLiteral("vad")), SttController::Mode::HandsFree);
        QCOMPARE(SttController::modeFromString(QStringLiteral("toggle")), SttController::Mode::Toggle);
        QCOMPARE(SttController::modeFromString(QStringLiteral("whatever")), SttController::Mode::PushToTalk);
        QCOMPARE(SttController::modeToString(SttController::Mode::HandsFree), QStringLiteral("vad"));
    }

    // --- Engine factory ---

    void factoryCreatesKnownEngines()
    {
        QTemporaryDir dir;
        Paths::setDataDirOverride(dir.path());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        SecretStore secrets(SecretStore::Backend::Memory);
        QNetworkAccessManager network;
        const EngineContext ctx{&network, &settings, &secrets};

        const QStringList ids = availableSttEngineIds();
        QVERIFY(ids.contains(QStringLiteral("openai")));
#ifdef VOCALINK_HAVE_WHISPER
        QCOMPARE(ids.first(), QStringLiteral("whisper"));
#else
        QVERIFY(!ids.contains(QStringLiteral("whisper")));
#endif
        std::unique_ptr<SttEngine> cloud(createSttEngine(QStringLiteral("openai"), ctx, nullptr));
        QCOMPARE(cloud->id(), QStringLiteral("openai"));
        QVERIFY(!cloud->isLocal());
        std::unique_ptr<SttEngine> fallback(createSttEngine(QStringLiteral("nonsense"), ctx, nullptr));
        QCOMPARE(fallback->id(), ids.first());
    }

    // --- WhisperEngine without a real model ---

    void whisperReportsMissingModel()
    {
#ifndef VOCALINK_HAVE_WHISPER
        QSKIP("Built without whisper.cpp");
#else
        QTemporaryDir dir;
        Paths::setDataDirOverride(dir.path());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        WhisperEngine engine(EngineContext{nullptr, &settings, nullptr});
        QCOMPARE(engine.id(), QStringLiteral("whisper"));
        QVERIFY(engine.isLocal());
        QVERIFY(!engine.isReady());
        QVERIFY(!engine.isLoading());
        QVERIFY(engine.notReadyReason().contains(QStringLiteral("Download")));
        QCOMPARE(engine.modelPath(), Paths::whisperModelsDir() + QStringLiteral("/ggml-base.en-q5_1.bin"));

        QSignalSpy failed(&engine, &SttEngine::failed);
        engine.transcribe(5, QVector<float>(16000, 0.0f));
        QCOMPARE(failed.count(), 0); // always asynchronous
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toULongLong(), quint64(5));
#endif
    }

    void whisperFollowsModelSettingAndRejectsBadFiles()
    {
#ifndef VOCALINK_HAVE_WHISPER
        QSKIP("Built without whisper.cpp");
#else
        QTemporaryDir dir;
        Paths::setDataDirOverride(dir.path());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        WhisperEngine engine(EngineContext{nullptr, &settings, nullptr});
        QSignalSpy loadFailed(&engine, &WhisperEngine::modelLoadFailed);

        QFile bad(Paths::whisperModelsDir() + QStringLiteral("/ggml-broken.bin"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("this is not a whisper model");
        bad.close();
        settings.setValue(Keys::WhisperModel, QStringLiteral("ggml-broken.bin"));
        QVERIFY(engine.isLoading());
        QCOMPARE(engine.notReadyReason(), QStringLiteral("Loading speech model…"));
        QTRY_COMPARE_WITH_TIMEOUT(loadFailed.count(), 1, 60000); // slow on CI macOS (Metal set-up)
        QVERIFY(!engine.isReady());
        QVERIFY(!engine.isLoading());
        QVERIFY(engine.notReadyReason().contains(QStringLiteral("ggml-broken.bin")));
#endif
    }

    void whisperPicksUpModelThatAppearsLater()
    {
#ifndef VOCALINK_HAVE_WHISPER
        QSKIP("Built without whisper.cpp");
#else
        QTemporaryDir dir;
        Paths::setDataDirOverride(dir.path());
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        WhisperEngine engine(EngineContext{nullptr, &settings, nullptr});
        QSignalSpy loadFailed(&engine, &WhisperEngine::modelLoadFailed);
        QVERIFY(!engine.isLoading());

        QFile later(engine.modelPath());
        QVERIFY(later.open(QIODevice::WriteOnly));
        later.write("still not a model");
        later.close();
        // The engine notices the new file and tries to load it.
        QTRY_COMPARE_WITH_TIMEOUT(loadFailed.count(), 1, 60000); // slow on CI macOS (Metal set-up)
        QCOMPARE(loadFailed.first().at(0).toString(), engine.modelPath());
#endif
    }

    // --- OpenAiSttEngine ---

    void cloudSendsMultipartRequest()
    {
        CloudRig rig;
        rig.secrets.set(Secrets::OpenAiStt, QStringLiteral("sk-stt"));
        rig.secrets.set(Secrets::OpenAi, QStringLiteral("sk-main"));
        rig.settings.setValue(Keys::SttOpenAiModel, QStringLiteral("whisper-1"));
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(R"({"text":" Hallo  Welt [BLANK_AUDIO] "})");
        });
        SttEngine::Options options;
        options.language = QStringLiteral("de-DE");
        options.prompt = QStringLiteral("Vocal Ink, Pikachu");
        rig.engine->setOptions(options);
        QVERIFY(rig.engine->isReady());

        QSignalSpy done(rig.engine.get(), &SttEngine::transcribed);
        QSignalSpy failed(rig.engine.get(), &SttEngine::failed);
        const QVector<float> audio = TestUtil::sine(440, 16000, 0.5);
        rig.engine->transcribe(7, audio);
        QTRY_COMPARE(done.count() + failed.count(), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(done.first().at(0).toULongLong(), quint64(7));
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("Hallo Welt"));

        QCOMPARE(rig.server.requests().size(), 1);
        const MockHttpServer::Request &req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/v1/audio/transcriptions"));
        QCOMPARE(req.header("authorization"), QByteArray("Bearer sk-stt"));
        const QByteArray type = req.header("content-type");
        QVERIFY2(type.startsWith("multipart/form-data"), type.constData());

        const QList<FormPart> parts = parseMultipart(type, req.body);
        const FormPart *model = findPart(parts, "model");
        const FormPart *language = findPart(parts, "language");
        const FormPart *prompt = findPart(parts, "prompt");
        const FormPart *format = findPart(parts, "response_format");
        const FormPart *file = findPart(parts, "file");
        QVERIFY(model && language && prompt && format && file);
        QCOMPARE(model->body, QByteArray("whisper-1"));
        QCOMPARE(language->body, QByteArray("de"));
        QCOMPARE(prompt->body, QByteArray("Vocal Ink, Pikachu"));
        QCOMPARE(format->body, QByteArray("json"));
        QCOMPARE(file->filename, QByteArray("speech.wav"));
        QCOMPARE(file->contentType, QByteArray("audio/wav"));

        const AudioConvert::WavHeader wav = AudioConvert::parseWavHeader(file->body);
        QVERIFY(wav.valid);
        QCOMPARE(wav.format.sampleRate(), 16000);
        QCOMPARE(wav.format.channelCount(), 1);
        QCOMPARE(wav.format.sampleFormat(), QAudioFormat::Int16);
        QCOMPARE(wav.dataSize, qint64(audio.size()) * 2);
        QCOMPARE(file->body.size(), wav.dataOffset + audio.size() * 2);
    }

    void cloudFallsBackToTtsKeyAndOmitsAutoLanguage()
    {
        CloudRig rig;
        rig.secrets.set(Secrets::OpenAi, QStringLiteral("sk-main"));
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(R"({"text":"ok then"})");
        });
        QVERIFY(rig.engine->isReady());
        QSignalSpy done(rig.engine.get(), &SttEngine::transcribed);
        rig.engine->transcribe(1, TestUtil::sine(440, 16000, 0.4));
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("ok then"));

        const MockHttpServer::Request &req = rig.server.requests().first();
        QCOMPARE(req.header("authorization"), QByteArray("Bearer sk-main"));
        const QList<FormPart> parts = parseMultipart(req.header("content-type"), req.body);
        QVERIFY(!findPart(parts, "language"));
        QVERIFY(!findPart(parts, "prompt"));
        QVERIFY(findPart(parts, "model"));
        QCOMPARE(findPart(parts, "model")->body, QByteArray("gpt-4o-mini-transcribe"));
    }

    void cloudMapsErrors()
    {
        CloudRig rig;
        rig.secrets.set(Secrets::OpenAiStt, QStringLiteral("sk-wrong"));
        QSignalSpy done(rig.engine.get(), &SttEngine::transcribed);
        QSignalSpy failed(rig.engine.get(), &SttEngine::failed);

        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(R"({"error":{"message":"Incorrect API key provided"}})", 401);
        });
        rig.engine->transcribe(1, TestUtil::sine(440, 16000, 0.4));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toULongLong(), quint64(1));
        QVERIFY2(failed.at(0).at(1).toString().contains(QStringLiteral("Incorrect API key provided")),
                 qPrintable(failed.at(0).at(1).toString()));

        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::bytes("upstream exploded", "text/plain", 500);
        });
        rig.engine->transcribe(2, TestUtil::sine(440, 16000, 0.4));
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY2(failed.at(1).at(1).toString().contains(QStringLiteral("500")), qPrintable(failed.at(1).at(1).toString()));

        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(R"({"unexpected":true})");
        });
        rig.engine->transcribe(3, TestUtil::sine(440, 16000, 0.4));
        QTRY_COMPARE(failed.count(), 3);
        QCOMPARE(failed.at(2).at(0).toULongLong(), quint64(3));
        QCOMPARE(done.count(), 0);
    }

    void cloudNeedsAKey()
    {
        CloudRig rig;
        QVERIFY(!rig.engine->isReady());
        QVERIFY(!rig.engine->notReadyReason().isEmpty());
        QSignalSpy failed(rig.engine.get(), &SttEngine::failed);
        QSignalSpy ready(rig.engine.get(), &SttEngine::readyChanged);
        rig.engine->transcribe(9, TestUtil::sine(440, 16000, 0.4));
        QCOMPARE(failed.count(), 0); // always asynchronous
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(1).toString(), rig.engine->notReadyReason());
        QVERIFY(rig.server.requests().isEmpty());

        rig.secrets.set(Secrets::OpenAiStt, QStringLiteral("sk-new"));
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready.first().first().toBool(), true);
        QVERIFY(rig.engine->isReady());
    }
};

QTEST_GUILESS_MAIN(TestStt)
#include "test_stt.moc"
