#include "audio/AudioConvert.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "support/MockHttpServer.h"
#include "tts/AzureTtsEngine.h"
#include "tts/ElevenLabsTtsEngine.h"
#include "tts/FishAudioTtsEngine.h"
#include "tts/OpenAiTtsEngine.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtEndian>

#include <algorithm>

namespace {

// Everything an engine needs, pointed at a local mock server.
struct Rig
{
    QTemporaryDir dir;
    Settings settings{dir.filePath(QStringLiteral("settings.ini")), nullptr};
    SecretStore secrets{SecretStore::Backend::Memory};
    QNetworkAccessManager network;
    MockHttpServer server;

    Rig() { server.listen(); }
    EngineContext context() { return EngineContext{&network, &settings, &secrets}; }
};

struct StreamResult
{
    QByteArray pcm;
    QAudioFormat format;
    QString error;
    bool finished = false;
    int chunks = 0;
};

StreamResult collect(TtsStream *stream, int timeoutMs = 5000)
{
    StreamResult r;
    QEventLoop loop;
    QObject::connect(stream, &TtsStream::audioReady, stream, [&r](const QAudioFormat &format, const QByteArray &pcm) {
        r.format = format;
        r.pcm += pcm;
        ++r.chunks;
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

// 16-bit little-endian mono samples with a recognisable pattern.
QByteArray testPcm(int samples)
{
    QByteArray pcm(samples * 2, Qt::Uninitialized);
    for (int i = 0; i < samples; ++i)
        qToLittleEndian<qint16>(qint16((i * 37) % 20000 - 10000), pcm.data() + 2 * i);
    return pcm;
}

MockHttpServer::Response pcmResponse(const QByteArray &pcm, int chunks)
{
    MockHttpServer::Response r = MockHttpServer::Response::bytes(pcm, "application/octet-stream");
    r.chunks = chunks;
    return r;
}

QJsonObject jsonBody(const MockHttpServer::Request &req)
{
    return QJsonDocument::fromJson(req.body).object();
}

Voice voice(const QString &engineId, const QString &id, const QString &language = {})
{
    Voice v;
    v.engineId = engineId;
    v.id = id;
    v.name = id;
    v.language = language;
    return v;
}

void verifyFormat(const QAudioFormat &format, int rate)
{
    QCOMPARE(format.sampleRate(), rate);
    QCOMPARE(format.channelCount(), 1);
    QCOMPARE(format.sampleFormat(), QAudioFormat::Int16);
}

const QByteArray kAzureVoices = R"json([
  {"Name": "Microsoft Server Speech Text to Speech Voice (en-US, JennyNeural)", "DisplayName": "Jenny",
   "LocalName": "Jenny", "ShortName": "en-US-JennyNeural", "Gender": "Female", "Locale": "en-US",
   "LocaleName": "English (United States)", "StyleList": ["assistant", "chat", "cheerful"],
   "SampleRateHertz": "24000", "VoiceType": "Neural", "Status": "GA"},
  {"Name": "Microsoft Server Speech Text to Speech Voice (de-DE, KatjaNeural)", "DisplayName": "Katja",
   "LocalName": "Katja", "ShortName": "de-DE-KatjaNeural", "Gender": "Female", "Locale": "de-DE",
   "SampleRateHertz": "48000", "VoiceType": "Neural", "Status": "GA"},
  {"Name": "Microsoft Server Speech Text to Speech Voice (ja-JP, KeitaNeural)", "DisplayName": "Keita",
   "LocalName": "圭太", "ShortName": "ja-JP-KeitaNeural", "Gender": "Male", "Locale": "ja-JP",
   "SampleRateHertz": "24000", "VoiceType": "Neural", "Status": "GA"},
  {"Name": "Microsoft Server Speech Text to Speech Voice (en-US, AndrewNeural)", "DisplayName": "Andrew",
   "LocalName": "Andrew", "ShortName": "en-US-AndrewNeural", "Gender": "Male", "Locale": "en-US",
   "SampleRateHertz": "24000", "VoiceType": "Neural", "Status": "GA"}
])json";

QByteArray elevenLabsVoice(const char *id, const char *name, const char *gender, const char *category)
{
    const QJsonObject o{
        {QStringLiteral("voice_id"), QLatin1String(id)},
        {QStringLiteral("name"), QLatin1String(name)},
        {QStringLiteral("category"), QLatin1String(category)},
        {QStringLiteral("labels"), QJsonObject{{QStringLiteral("accent"), QStringLiteral("american")},
                                               {QStringLiteral("gender"), QLatin1String(gender)},
                                               {QStringLiteral("age"), QStringLiteral("middle_aged")},
                                               {QStringLiteral("use_case"), QStringLiteral("narration")}}},
        {QStringLiteral("preview_url"), QStringLiteral("https://storage.example/%1.mp3").arg(QLatin1String(id))},
    };
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QJsonObject fishModel(const QString &id, const QString &title, const QStringList &tags, const QString &language)
{
    return QJsonObject{
        {QStringLiteral("_id"), id},
        {QStringLiteral("type"), QStringLiteral("tts")},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), QStringLiteral("A long description of %1").arg(title)},
        {QStringLiteral("languages"), QJsonArray{language}},
        {QStringLiteral("tags"), QJsonArray::fromStringList(tags)},
        {QStringLiteral("samples"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("Sample")},
                                                           {QStringLiteral("audio"), QStringLiteral("https://cdn.example/%1.mp3").arg(id)}}}},
        {QStringLiteral("task_count"), 1234},
    };
}

QByteArray fishPage(const QList<QJsonObject> &items)
{
    QJsonArray arr;
    for (const QJsonObject &o : items)
        arr.append(o);
    return QJsonDocument(QJsonObject{{QStringLiteral("total"), int(items.size())}, {QStringLiteral("items"), arr}})
        .toJson(QJsonDocument::Compact);
}

} // namespace

class TestTtsCloud : public QObject
{
    Q_OBJECT
private slots:
    // --- Azure -----------------------------------------------------------------

    void azureNeedsKey()
    {
        Rig rig;
        AzureTtsEngine engine(rig.context());
        QCOMPARE(engine.id(), QStringLiteral("azure"));
        QVERIFY(!engine.isLocal());
        QVERIFY(!engine.isAvailable());
        QVERIFY(engine.unavailableReason().contains(QLatin1String("Azure")));
        rig.secrets.set(Secrets::Azure, QStringLiteral("az-key"));
        QVERIFY(engine.isAvailable());
        QVERIFY(engine.unavailableReason().isEmpty());
    }

    void azureSsml()
    {
        SpeakOptions defaults;
        const QString plain = AzureTtsEngine::buildSsml(QStringLiteral("Hi"), QStringLiteral("en-US-JennyNeural"),
                                                        QStringLiteral("en-US"), defaults);
        QVERIFY(!plain.contains(QLatin1String("prosody")));
        QVERIFY(plain.contains(QLatin1String("<voice name='en-US-JennyNeural'>Hi</voice>")));

        SpeakOptions slow;
        slow.rate = 0.5;
        slow.pitch = -1.0;
        const QString s = AzureTtsEngine::buildSsml(QStringLiteral("x"), QStringLiteral("v"), QStringLiteral("en-US"), slow);
        QVERIFY2(s.contains(QLatin1String("<prosody rate='-50%' pitch='-6st'>x</prosody>")), qPrintable(s));

        SpeakOptions fast;
        fast.rate = 1.5;
        const QString f = AzureTtsEngine::buildSsml(QStringLiteral("x"), QStringLiteral("v"), QStringLiteral("en-US"), fast);
        QVERIFY2(f.contains(QLatin1String("<prosody rate='+50%'>")), qPrintable(f));
        QVERIFY(!f.contains(QLatin1String("pitch=")));
    }

    void azureSynthesizes()
    {
        Rig rig;
        rig.secrets.set(Secrets::Azure, QStringLiteral("az-key"));
        const QByteArray pcm = testPcm(4801); // odd byte split points across chunks
        rig.server.setHandler([&pcm](const MockHttpServer::Request &) { return pcmResponse(pcm, 6); });

        AzureTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        SpeakOptions options;
        options.rate = 1.5;
        options.pitch = 0.5;
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Tom & Jerry <3 > \"all\""),
                                                         voice(QStringLiteral("azure"), QStringLiteral("en-US-JennyNeural"), QStringLiteral("en-US")),
                                                         options));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QCOMPARE(r.pcm, pcm);
        QVERIFY(r.chunks >= 2); // streamed, not buffered
        verifyFormat(r.format, 24000);

        QCOMPARE(rig.server.requests().size(), 1);
        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/cognitiveservices/v1"));
        QCOMPARE(req.header("Ocp-Apim-Subscription-Key"), QByteArray("az-key"));
        QCOMPARE(req.header("Content-Type"), QByteArray("application/ssml+xml"));
        QCOMPARE(req.header("X-Microsoft-OutputFormat"), QByteArray("raw-24khz-16bit-mono-pcm"));
        QVERIFY(req.header("User-Agent").startsWith("VocalInk/"));
        const QString ssml = QString::fromUtf8(req.body);
        QVERIFY2(ssml.startsWith(QLatin1String("<speak version='1.0'")), qPrintable(ssml));
        QVERIFY(ssml.contains(QLatin1String("xml:lang='en-US'")));
        QVERIFY(ssml.contains(QLatin1String("<voice name='en-US-JennyNeural'>")));
        QVERIFY(ssml.contains(QLatin1String("<prosody rate='+50%' pitch='+3st'>")));
        QVERIFY2(ssml.contains(QLatin1String("Tom &amp; Jerry &lt;3 &gt; &quot;all&quot;</prosody>")), qPrintable(ssml));
        QVERIFY(ssml.endsWith(QLatin1String("</voice></speak>")));
    }

    void azureLocaleFromShortName()
    {
        Rig rig;
        rig.secrets.set(Secrets::Azure, QStringLiteral("az-key"));
        rig.server.setHandler([](const MockHttpServer::Request &) { return pcmResponse(testPcm(10), 1); });
        AzureTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        // A voice restored from settings before the list has loaded.
        const Voice restored = Voice::fromKey(QStringLiteral("azure:de-DE-KatjaNeural"));
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hallo"), restored, SpeakOptions()));
        QVERIFY(r.finished);
        QVERIFY(QString::fromUtf8(rig.server.requests().first().body).contains(QLatin1String("xml:lang='de-DE'")));
    }

    void azureVoiceList()
    {
        Rig rig;
        rig.secrets.set(Secrets::Azure, QStringLiteral("az-key"));
        rig.server.setHandler([](const MockHttpServer::Request &) { return MockHttpServer::Response::json(kAzureVoices); });
        AzureTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QVERIFY(engine.isRefreshing());
        QVERIFY(changed.wait(5000));
        QVERIFY(!engine.isRefreshing());

        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("GET"));
        QCOMPARE(req.path, QStringLiteral("/cognitiveservices/voices/list"));
        QCOMPARE(req.header("Ocp-Apim-Subscription-Key"), QByteArray("az-key"));

        const QList<Voice> voices = engine.voices();
        QCOMPARE(voices.size(), 4);
        // Sorted by locale, then name.
        QCOMPARE(voices.at(0).id, QStringLiteral("de-DE-KatjaNeural"));
        QCOMPARE(voices.at(1).id, QStringLiteral("en-US-AndrewNeural"));
        QCOMPARE(voices.at(2).id, QStringLiteral("en-US-JennyNeural"));
        QCOMPARE(voices.at(3).id, QStringLiteral("ja-JP-KeitaNeural"));
        const Voice &jenny = voices.at(2);
        QCOMPARE(jenny.engineId, QStringLiteral("azure"));
        QCOMPARE(jenny.name, QStringLiteral("Jenny"));
        QCOMPARE(jenny.language, QStringLiteral("en-US"));
        QCOMPARE(jenny.gender, QStringLiteral("Female"));
        QCOMPARE(jenny.description, QStringLiteral("Styles: assistant, chat, cheerful"));
        QCOMPARE(voices.at(3).name, QStringLiteral("Keita (%1)").arg(QString::fromUtf8("\xe5\x9c\xad\xe5\xa4\xaa")));
        QCOMPARE(voices.at(3).gender, QStringLiteral("Male"));
        QVERIFY(voices.at(0).description.isEmpty());
    }

    void azureErrors()
    {
        Rig rig;
        rig.secrets.set(Secrets::Azure, QStringLiteral("bad"));
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::bytes(QByteArray(), "text/plain", 401);
        });
        AzureTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), voice(QStringLiteral("azure"), QStringLiteral("en-US-JennyNeural")), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY2(r.error.contains(QLatin1String("API key")), qPrintable(r.error));
        QVERIFY(r.pcm.isEmpty());

        QSignalSpy errors(&engine, &TtsEngine::voicesError);
        engine.refreshVoices();
        QVERIFY(errors.wait(5000));
        QVERIFY(errors.first().at(0).toString().contains(QLatin1String("API key")));
        QVERIFY(!engine.isRefreshing());
    }

    // --- ElevenLabs --------------------------------------------------------------

    void elevenLabsSynthesizes()
    {
        Rig rig;
        QVERIFY(!ElevenLabsTtsEngine(rig.context()).isAvailable());
        rig.secrets.set(Secrets::ElevenLabs, QStringLiteral("el-key"));
        const QByteArray pcm = testPcm(3000);
        rig.server.setHandler([&pcm](const MockHttpServer::Request &) { return pcmResponse(pcm, 3); });

        ElevenLabsTtsEngine engine(rig.context());
        QVERIFY(engine.isAvailable());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        SpeakOptions options;
        options.rate = 2.0;
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hello there"), voice(QStringLiteral("elevenlabs"), QStringLiteral("21m00Tcm4TlvDq8ikWAM")), options));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QCOMPARE(r.pcm, pcm);
        verifyFormat(r.format, 24000);

        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/v1/text-to-speech/21m00Tcm4TlvDq8ikWAM/stream"));
        QCOMPARE(req.query.queryItemValue(QStringLiteral("output_format")), QStringLiteral("pcm_24000"));
        QCOMPARE(req.header("xi-api-key"), QByteArray("el-key"));
        QCOMPARE(req.header("Content-Type"), QByteArray("application/json"));
        const QJsonObject body = jsonBody(req);
        QCOMPARE(body.value(QStringLiteral("text")).toString(), QStringLiteral("Hello there"));
        QCOMPARE(body.value(QStringLiteral("model_id")).toString(), QStringLiteral("eleven_flash_v2_5"));
        const QJsonObject settings = body.value(QStringLiteral("voice_settings")).toObject();
        QCOMPARE(settings.value(QStringLiteral("stability")).toDouble(), 0.5);
        QCOMPARE(settings.value(QStringLiteral("similarity_boost")).toDouble(), 0.75);
        QCOMPARE(settings.value(QStringLiteral("speed")).toDouble(), 1.2); // clamped

        rig.settings.setValue(Keys::ElevenLabsModel, QStringLiteral("eleven_multilingual_v2"));
        options.rate = 0.5;
        collect(engine.synthesize(QStringLiteral("Again"), voice(QStringLiteral("elevenlabs"), QStringLiteral("abc")), options));
        const QJsonObject second = jsonBody(rig.server.requests().at(1));
        QCOMPARE(second.value(QStringLiteral("model_id")).toString(), QStringLiteral("eleven_multilingual_v2"));
        QCOMPARE(second.value(QStringLiteral("voice_settings")).toObject().value(QStringLiteral("speed")).toDouble(), 0.7);
    }

    void elevenLabsVoicePages()
    {
        Rig rig;
        rig.secrets.set(Secrets::ElevenLabs, QStringLiteral("el-key"));
        rig.server.setHandler([](const MockHttpServer::Request &req) {
            if (req.query.queryItemValue(QStringLiteral("next_page_token")) == QLatin1String("tok2")) {
                return MockHttpServer::Response::json("{\"voices\":[" + elevenLabsVoice("v3", "Clyde", "male", "premade")
                                                      + "],\"has_more\":false,\"total_count\":3}");
            }
            return MockHttpServer::Response::json("{\"voices\":[" + elevenLabsVoice("v1", "Rachel", "female", "premade") + ","
                                                  + elevenLabsVoice("v2", "My Clone", "", "cloned")
                                                  + "],\"has_more\":true,\"total_count\":3,\"next_page_token\":\"tok2\"}");
        });
        ElevenLabsTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QVERIFY(changed.wait(5000));

        QCOMPARE(rig.server.requests().size(), 2);
        const MockHttpServer::Request first = rig.server.requests().at(0);
        QCOMPARE(first.method, QByteArray("GET"));
        QCOMPARE(first.path, QStringLiteral("/v2/voices"));
        QCOMPARE(first.query.queryItemValue(QStringLiteral("page_size")), QStringLiteral("100"));
        QVERIFY(!first.query.hasQueryItem(QStringLiteral("next_page_token")));
        QCOMPARE(first.header("xi-api-key"), QByteArray("el-key"));
        QCOMPARE(rig.server.requests().at(1).query.queryItemValue(QStringLiteral("next_page_token")), QStringLiteral("tok2"));

        const QList<Voice> voices = engine.voices();
        QCOMPARE(voices.size(), 3);
        QCOMPARE(voices.at(0).id, QStringLiteral("v1"));
        QCOMPARE(voices.at(0).name, QStringLiteral("Rachel"));
        QCOMPARE(voices.at(0).gender, QStringLiteral("Female"));
        QCOMPARE(voices.at(0).description, QStringLiteral("american, middle aged, narration"));
        QCOMPARE(voices.at(0).previewUrl, QStringLiteral("https://storage.example/v1.mp3"));
        QCOMPARE(voices.at(1).description, QStringLiteral("cloned, american, middle aged, narration"));
        QVERIFY(voices.at(1).gender.isEmpty());
        QCOMPARE(voices.at(2).id, QStringLiteral("v3"));
        QCOMPARE(voices.at(2).gender, QStringLiteral("Male"));
    }

    void elevenLabsPageLimit()
    {
        Rig rig;
        rig.secrets.set(Secrets::ElevenLabs, QStringLiteral("el-key"));
        int page = 0;
        rig.server.setHandler([&page](const MockHttpServer::Request &) {
            ++page;
            const QByteArray id = "p" + QByteArray::number(page);
            return MockHttpServer::Response::json("{\"voices\":[" + elevenLabsVoice(id.constData(), "V", "male", "premade")
                                                  + "],\"has_more\":true,\"next_page_token\":\"" + id + "\"}");
        });
        ElevenLabsTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QVERIFY(changed.wait(5000));
        QCOMPARE(rig.server.requests().size(), 5);
        QCOMPARE(engine.voices().size(), 5);
    }

    void elevenLabsCustomVoices()
    {
        Rig rig;
        rig.settings.setValue(Keys::ElevenLabsCustomVoices, QStringList{QStringLiteral("cust1|My Friend")});
        {
            ElevenLabsTtsEngine engine(rig.context());
            QVERIFY(engine.supportsCustomVoiceIds());
            // Listed right away, without a refresh or even a key.
            QCOMPARE(engine.voices().size(), 1);
            QCOMPARE(engine.voices().first().id, QStringLiteral("cust1"));
            QCOMPARE(engine.voices().first().name, QStringLiteral("My Friend"));

            QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
            engine.addCustomVoice(QStringLiteral(" v1 "), QStringLiteral("Rachel pasted"));
            QCOMPARE(changed.size(), 1);
            QCOMPARE(engine.voices().size(), 2);
            engine.addCustomVoice(QStringLiteral("cust1"), QStringLiteral("Renamed")); // replaces
            QCOMPARE(engine.voices().size(), 2);
            QCOMPARE(rig.settings.value(Keys::ElevenLabsCustomVoices).toStringList(),
                     (QStringList{QStringLiteral("cust1|Renamed"), QStringLiteral("v1|Rachel pasted")}));

            // A pasted id that the account listing also has shows up once, with the listing's data.
            rig.secrets.set(Secrets::ElevenLabs, QStringLiteral("el-key"));
            rig.server.setHandler([](const MockHttpServer::Request &) {
                return MockHttpServer::Response::json("{\"voices\":[" + elevenLabsVoice("v1", "Rachel", "female", "premade")
                                                      + "],\"has_more\":false}");
            });
            engine.setBaseUrlOverride(rig.server.baseUrl());
            engine.refreshVoices();
            QVERIFY(changed.wait(5000));
            QCOMPARE(engine.voices().size(), 2);
            QCOMPARE(engine.voices().at(0).id, QStringLiteral("cust1"));
            QCOMPARE(engine.voices().at(1).id, QStringLiteral("v1"));
            QCOMPARE(engine.voices().at(1).name, QStringLiteral("Rachel"));

            engine.removeCustomVoice(QStringLiteral("cust1"));
            QCOMPARE(engine.voices().size(), 1);
            QCOMPARE(rig.settings.value(Keys::ElevenLabsCustomVoices).toStringList(),
                     QStringList{QStringLiteral("v1|Rachel pasted")});
        }
        ElevenLabsTtsEngine reopened(rig.context());
        QCOMPARE(reopened.voices().size(), 1);
        QCOMPARE(reopened.voices().first().name, QStringLiteral("Rachel pasted"));
    }

    void elevenLabsErrors()
    {
        Rig rig;
        rig.secrets.set(Secrets::ElevenLabs, QStringLiteral("el-key"));
        rig.server.setHandler([](const MockHttpServer::Request &req) {
            if (req.method == "GET") {
                return MockHttpServer::Response::json(
                    R"({"detail":{"status":"invalid_api_key","message":"Invalid API key"}})", 401);
            }
            return MockHttpServer::Response::json(
                R"({"detail":{"status":"voice_not_found","message":"A voice with voice_id nope does not exist."}})", 400);
        });
        ElevenLabsTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), voice(QStringLiteral("elevenlabs"), QStringLiteral("nope")), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY2(r.error.contains(QLatin1String("does not exist")), qPrintable(r.error));

        QSignalSpy errors(&engine, &TtsEngine::voicesError);
        engine.refreshVoices();
        QVERIFY(errors.wait(5000));
        const QString message = errors.first().at(0).toString();
        QVERIFY2(message.contains(QLatin1String("API key")), qPrintable(message));
        QVERIFY(message.contains(QLatin1String("Invalid API key")));
    }

    // --- Fish Audio --------------------------------------------------------------

    void fishSynthesizesStreamedWav()
    {
        Rig rig;
        QVERIFY(!FishAudioTtsEngine(rig.context()).isAvailable());
        rig.secrets.set(Secrets::FishAudio, QStringLiteral("fish-key"));
        const QByteArray pcm = testPcm(5000);
        QByteArray wav = AudioConvert::makeWav(pcm, AudioConvert::int16Mono(24000));
        // A streamed WAV does not know its sizes up front.
        qToLittleEndian<quint32>(0xFFFFFFFFu, wav.data() + 4);
        qToLittleEndian<quint32>(0xFFFFFFFFu, wav.data() + 40);
        rig.server.setHandler([&wav](const MockHttpServer::Request &) {
            MockHttpServer::Response resp = MockHttpServer::Response::bytes(wav, "audio/wav");
            resp.chunks = 5;
            return resp;
        });

        FishAudioTtsEngine engine(rig.context());
        QVERIFY(engine.isAvailable());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        SpeakOptions options;
        options.rate = 0.5;
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hello fish"), voice(QStringLiteral("fishaudio"), QStringLiteral("model-abc")), options));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QCOMPARE(r.pcm, pcm);
        verifyFormat(r.format, 24000);

        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/v1/tts"));
        QCOMPARE(req.header("Authorization"), QByteArray("Bearer fish-key"));
        QCOMPARE(req.header("model"), QByteArray("s1"));
        QCOMPARE(req.header("Content-Type"), QByteArray("application/json"));
        const QJsonObject body = jsonBody(req);
        QCOMPARE(body.value(QStringLiteral("text")).toString(), QStringLiteral("Hello fish"));
        QCOMPARE(body.value(QStringLiteral("reference_id")).toString(), QStringLiteral("model-abc"));
        QCOMPARE(body.value(QStringLiteral("format")).toString(), QStringLiteral("wav"));
        QCOMPARE(body.value(QStringLiteral("sample_rate")).toInt(), 24000);
        QCOMPARE(body.value(QStringLiteral("latency")).toString(), QStringLiteral("balanced"));
        QCOMPARE(body.value(QStringLiteral("normalize")).toBool(), true);
        QCOMPARE(body.value(QStringLiteral("prosody")).toObject().value(QStringLiteral("speed")).toDouble(), 0.5);
    }

    void fishVoiceList()
    {
        Rig rig;
        rig.secrets.set(Secrets::FishAudio, QStringLiteral("fish-key"));
        rig.settings.setValue(Keys::FishCustomVoices,
                              QStringList{QStringLiteral("pasted1|Pasted voice"), QStringLiteral("own1|Also mine")});
        rig.server.setHandler([](const MockHttpServer::Request &req) {
            if (req.query.queryItemValue(QStringLiteral("self")) == QLatin1String("true"))
                return MockHttpServer::Response::json(fishPage({fishModel(QStringLiteral("own1"), QStringLiteral("My voice"), {}, QStringLiteral("en"))}));
            return MockHttpServer::Response::json(fishPage({
                fishModel(QStringLiteral("pop1"), QStringLiteral("Energetic Male"), {QStringLiteral("male"), QStringLiteral("young")}, QStringLiteral("en")),
                fishModel(QStringLiteral("own1"), QStringLiteral("My voice"), {}, QStringLiteral("en")),
                fishModel(QStringLiteral("pop2"), QStringLiteral("Calm"), {QStringLiteral("Female")}, QStringLiteral("zh")),
            }));
        });
        FishAudioTtsEngine engine(rig.context());
        QCOMPARE(engine.voices().size(), 2); // pasted ids before any refresh
        engine.setBaseUrlOverride(rig.server.baseUrl());
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QVERIFY(changed.wait(5000));

        QCOMPARE(rig.server.requests().size(), 2);
        bool sawSelf = false;
        bool sawPopular = false;
        for (const MockHttpServer::Request &req : rig.server.requests()) {
            QCOMPARE(req.method, QByteArray("GET"));
            QCOMPARE(req.path, QStringLiteral("/model"));
            QCOMPARE(req.header("Authorization"), QByteArray("Bearer fish-key"));
            if (req.query.queryItemValue(QStringLiteral("self")) == QLatin1String("true")) {
                sawSelf = true;
                QCOMPARE(req.query.queryItemValue(QStringLiteral("page_size")), QStringLiteral("100"));
            } else {
                sawPopular = true;
                QCOMPARE(req.query.queryItemValue(QStringLiteral("page_size")), QStringLiteral("50"));
                QCOMPARE(req.query.queryItemValue(QStringLiteral("sort_by")), QStringLiteral("task_count"));
            }
        }
        QVERIFY(sawSelf && sawPopular);

        const QList<Voice> voices = engine.voices();
        QStringList ids;
        for (const Voice &v : voices)
            ids << v.id;
        QCOMPARE(ids, (QStringList{QStringLiteral("pasted1"), QStringLiteral("own1"), QStringLiteral("pop1"), QStringLiteral("pop2")}));
        QCOMPARE(voices.at(0).name, QStringLiteral("Pasted voice"));
        const Voice &own = voices.at(1);
        QCOMPARE(own.name, QStringLiteral("My voice"));
        QVERIFY(own.description.startsWith(QLatin1String("Your model")));
        QVERIFY(own.description.contains(QLatin1String("A long description of My voice")));
        QCOMPARE(own.language, QStringLiteral("en"));
        QCOMPARE(own.previewUrl, QStringLiteral("https://cdn.example/own1.mp3"));
        QCOMPARE(voices.at(2).description, QStringLiteral("male, young"));
        QCOMPARE(voices.at(2).gender, QStringLiteral("Male"));
        QCOMPARE(voices.at(3).gender, QStringLiteral("Female"));
        QCOMPARE(voices.at(3).language, QStringLiteral("zh"));
    }

    void fishSearchAndCustomVoices()
    {
        Rig rig;
        rig.secrets.set(Secrets::FishAudio, QStringLiteral("fish-key"));
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(fishPage({fishModel(QStringLiteral("found1"), QStringLiteral("Narrator & Co"), {}, QStringLiteral("en"))}));
        });
        FishAudioTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        QVERIFY(engine.supportsRemoteSearch());
        QSignalSpy found(&engine, &TtsEngine::searchFinished);
        engine.searchVoices(QStringLiteral("deep narrator"));
        QVERIFY(found.wait(5000));
        QCOMPARE(found.first().at(0).toString(), QStringLiteral("deep narrator"));
        const QList<Voice> results = found.first().at(1).value<QList<Voice>>();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().id, QStringLiteral("found1"));
        QCOMPARE(results.first().engineId, QStringLiteral("fishaudio"));
        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.path, QStringLiteral("/model"));
        QCOMPARE(req.query.queryItemValue(QStringLiteral("title")), QStringLiteral("deep narrator"));
        QCOMPARE(req.query.queryItemValue(QStringLiteral("page_size")), QStringLiteral("30"));
        QCOMPARE(req.query.queryItemValue(QStringLiteral("sort_by")), QStringLiteral("task_count"));
        QVERIFY(engine.voices().isEmpty()); // searching doesn't change the list

        engine.searchVoices(QStringLiteral("   "));
        QVERIFY(found.wait(2000));
        QVERIFY(found.at(1).at(1).value<QList<Voice>>().isEmpty());
        QCOMPARE(rig.server.requests().size(), 1);

        QVERIFY(engine.supportsCustomVoiceIds());
        engine.addCustomVoice(QStringLiteral("found1"), QStringLiteral("Narrator"));
        QCOMPARE(engine.voices().size(), 1);
        QCOMPARE(rig.settings.value(Keys::FishCustomVoices).toStringList(), QStringList{QStringLiteral("found1|Narrator")});
        engine.removeCustomVoice(QStringLiteral("found1"));
        QVERIFY(engine.voices().isEmpty());
        QVERIFY(rig.settings.value(Keys::FishCustomVoices).toStringList().isEmpty());
    }

    void fishErrors()
    {
        Rig rig;
        rig.secrets.set(Secrets::FishAudio, QStringLiteral("fish-key"));
        rig.server.setHandler([](const MockHttpServer::Request &req) {
            if (req.method == "GET")
                return MockHttpServer::Response::json(R"({"status":401,"message":"Invalid token"})", 401);
            return MockHttpServer::Response::json(R"({"status":402,"message":"Insufficient balance"})", 402);
        });
        FishAudioTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), voice(QStringLiteral("fishaudio"), QStringLiteral("m")), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY2(r.error.contains(QLatin1String("Insufficient balance")), qPrintable(r.error));

        QSignalSpy errors(&engine, &TtsEngine::voicesError);
        engine.refreshVoices();
        QVERIFY(errors.wait(5000));
        QVERIFY(errors.first().at(0).toString().contains(QLatin1String("API key")));
        QVERIFY(!engine.isRefreshing());
    }

    // --- OpenAI ------------------------------------------------------------------

    void openAiAvailability()
    {
        Rig rig;
        OpenAiTtsEngine engine(rig.context());
        QVERIFY(!engine.isAvailable());
        QVERIFY(engine.unavailableReason().contains(QLatin1String("OpenAI")));
        rig.secrets.set(Secrets::OpenAi, QStringLiteral("sk-test"));
        QVERIFY(engine.isAvailable());
        rig.secrets.set(Secrets::OpenAi, QString());
        QVERIFY(!engine.isAvailable());

        // A self-hosted compatible server usually needs no key.
        QSignalSpy availability(&engine, &TtsEngine::availabilityChanged);
        rig.settings.setValue(Keys::OpenAiBaseUrl, QStringLiteral("http://localhost:8880/v1"));
        QCOMPARE(availability.size(), 1);
        QVERIFY(engine.isAvailable());
    }

    void openAiVoices()
    {
        Rig rig;
        OpenAiTtsEngine engine(rig.context());
        QCOMPARE(engine.voices().size(), 13);
        QSignalSpy changed(&engine, &TtsEngine::voicesChanged);
        engine.refreshVoices();
        QCOMPARE(changed.size(), 1);
        const QList<Voice> voices = engine.voices();
        QCOMPARE(voices.size(), 13);
        QCOMPARE(voices.first().id, QStringLiteral("alloy"));
        QCOMPARE(voices.first().name, QStringLiteral("Alloy"));
        QVERIFY(voices.first().language.isEmpty());
        const auto marin = std::find_if(voices.cbegin(), voices.cend(), [](const Voice &v) { return v.id == QLatin1String("marin"); });
        QVERIFY(marin != voices.cend());
        QCOMPARE(marin->description, QStringLiteral("Recommended"));
        for (const Voice &v : voices)
            QCOMPARE(v.engineId, QStringLiteral("openai"));
    }

    void openAiSynthesizes()
    {
        Rig rig;
        rig.secrets.set(Secrets::OpenAi, QStringLiteral("sk-test"));
        const QByteArray pcm = testPcm(2400);
        rig.server.setHandler([&pcm](const MockHttpServer::Request &) { return pcmResponse(pcm, 4); });
        OpenAiTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        SpeakOptions options;
        options.rate = 1.5;
        options.instructions = QStringLiteral("Speak cheerfully");
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Good morning"), voice(QStringLiteral("openai"), QStringLiteral("marin")), options));
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.finished);
        QCOMPARE(r.pcm, pcm);
        verifyFormat(r.format, 24000);

        const MockHttpServer::Request req = rig.server.requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/audio/speech"));
        QCOMPARE(req.header("Authorization"), QByteArray("Bearer sk-test"));
        QCOMPARE(req.header("Content-Type"), QByteArray("application/json"));
        const QJsonObject body = jsonBody(req);
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o-mini-tts"));
        QCOMPARE(body.value(QStringLiteral("input")).toString(), QStringLiteral("Good morning"));
        QCOMPARE(body.value(QStringLiteral("voice")).toString(), QStringLiteral("marin"));
        QCOMPARE(body.value(QStringLiteral("response_format")).toString(), QStringLiteral("pcm"));
        QCOMPARE(body.value(QStringLiteral("speed")).toDouble(), 1.5);
        QCOMPARE(body.value(QStringLiteral("instructions")).toString(), QStringLiteral("Speak cheerfully"));

        // tts-1 models don't take instructions; empty instructions are never sent.
        rig.settings.setValue(Keys::OpenAiTtsModel, QStringLiteral("tts-1-hd"));
        collect(engine.synthesize(QStringLiteral("Again"), voice(QStringLiteral("openai"), QStringLiteral("alloy")), options));
        QCOMPARE(jsonBody(rig.server.requests().at(1)).value(QStringLiteral("model")).toString(), QStringLiteral("tts-1-hd"));
        QVERIFY(!jsonBody(rig.server.requests().at(1)).contains(QStringLiteral("instructions")));
        rig.settings.setValue(Keys::OpenAiTtsModel, QStringLiteral("gpt-4o-mini-tts"));
        options.instructions.clear();
        collect(engine.synthesize(QStringLiteral("Third"), voice(QStringLiteral("openai"), QStringLiteral("alloy")), options));
        QVERIFY(!jsonBody(rig.server.requests().at(2)).contains(QStringLiteral("instructions")));
    }

    void openAiErrors()
    {
        Rig rig;
        rig.secrets.set(Secrets::OpenAi, QStringLiteral("sk-wrong"));
        rig.server.setHandler([](const MockHttpServer::Request &) {
            return MockHttpServer::Response::json(
                R"({"error":{"message":"Incorrect API key provided: sk-wrong.","type":"invalid_request_error","code":"invalid_api_key"}})", 401);
        });
        OpenAiTtsEngine engine(rig.context());
        engine.setBaseUrlOverride(rig.server.baseUrl());
        const StreamResult r = collect(engine.synthesize(QStringLiteral("Hi"), voice(QStringLiteral("openai"), QStringLiteral("alloy")), SpeakOptions()));
        QVERIFY(!r.finished);
        QVERIFY2(r.error.contains(QLatin1String("did not accept the API key")), qPrintable(r.error));
        QVERIFY(r.error.contains(QLatin1String("Incorrect API key provided")));
    }
};

QTEST_GUILESS_MAIN(TestTtsCloud)
#include "test_tts_cloud.moc"
