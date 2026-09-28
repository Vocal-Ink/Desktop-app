#include "tts/ElevenLabsTtsEngine.h"

#include "audio/AudioConvert.h"
#include "core/NetworkUtil.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "tts/CloudHelpers.h"
#include "tts/StreamHelpers.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace {
constexpr int kMaxPages = 5;
}

ElevenLabsTtsEngine::ElevenLabsTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
    // Pasted voice ids are usable before the first refresh.
    m_voices = CloudTts::customVoices(m_ctx.settings, Keys::ElevenLabsCustomVoices, id());
}

bool ElevenLabsTtsEngine::isAvailable() const
{
    return m_ctx.secrets && m_ctx.secrets->has(Secrets::ElevenLabs);
}

QString ElevenLabsTtsEngine::unavailableReason() const
{
    return isAvailable() ? QString() : tr("Add your ElevenLabs API key in Settings → Voices.");
}

QNetworkRequest ElevenLabsTtsEngine::request(const QString &pathAndQuery) const
{
    const QString base = baseUrl(QStringLiteral("https://api.elevenlabs.io"));
    QNetworkRequest req = NetworkUtil::jsonRequest(QUrl(base + pathAndQuery));
    req.setRawHeader("xi-api-key", m_ctx.secrets->get(Secrets::ElevenLabs).toUtf8());
    return req;
}

void ElevenLabsTtsEngine::refreshVoices()
{
    if (!isAvailable()) {
        m_remote.clear();
        publishVoices();
        return;
    }
    setRefreshing(true);
    m_loading.clear();
    fetchPage(++m_refreshGeneration, 0, QString());
}

void ElevenLabsTtsEngine::fetchPage(int generation, int page, const QString &pageToken)
{
    QString path = QStringLiteral("/v2/voices?page_size=100");
    if (!pageToken.isEmpty())
        path += QStringLiteral("&next_page_token=") + QString::fromLatin1(QUrl::toPercentEncoding(pageToken));

    CloudTts::getJson(m_ctx.network, request(path), displayName(), this,
                      [this, generation, page](const QJsonDocument &json, const QString &error) {
        if (generation != m_refreshGeneration)
            return;
        if (!error.isEmpty()) {
            m_loading.clear();
            setRefreshing(false);
            emit voicesError(error);
            return;
        }
        const QJsonObject root = json.object();
        const QJsonArray arr = root.value(QStringLiteral("voices")).toArray();
        for (const QJsonValue &value : arr) {
            const QJsonObject o = value.toObject();
            Voice v;
            v.engineId = id();
            v.id = o.value(QStringLiteral("voice_id")).toString();
            if (v.id.isEmpty())
                continue;
            v.name = o.value(QStringLiteral("name")).toString().trimmed();
            if (v.name.isEmpty())
                v.name = v.id;
            v.previewUrl = o.value(QStringLiteral("preview_url")).toString();

            const QJsonObject labels = o.value(QStringLiteral("labels")).toObject();
            v.gender = CloudTts::normalizeGender(labels.value(QStringLiteral("gender")).toString());
            v.language = labels.value(QStringLiteral("language")).toString();
            QStringList details;
            const QString category = o.value(QStringLiteral("category")).toString();
            if (!category.isEmpty() && category != QLatin1String("premade"))
                details << category;
            for (const auto *key : {"accent", "age", "description", "use_case"}) {
                const QString label = labels.value(QLatin1String(key)).toString().replace(QLatin1Char('_'), QLatin1Char(' '));
                if (!label.trimmed().isEmpty())
                    details << label.trimmed();
            }
            v.description = details.join(QStringLiteral(", "));
            m_loading << v;
        }

        const QString next = root.value(QStringLiteral("next_page_token")).toString();
        if (root.value(QStringLiteral("has_more")).toBool() && !next.isEmpty() && page + 1 < kMaxPages) {
            fetchPage(generation, page + 1, next);
            return;
        }
        m_remote = m_loading;
        m_loading.clear();
        publishVoices();
    });
}

void ElevenLabsTtsEngine::publishVoices()
{
    // Pasted ids first (unless the account listing already has them), then the library.
    QList<Voice> list;
    const QList<Voice> custom = CloudTts::customVoices(m_ctx.settings, Keys::ElevenLabsCustomVoices, id());
    for (const Voice &v : custom) {
        if (!m_remote.contains(v))
            list << v;
    }
    CloudTts::mergeVoices(list, m_remote);
    setVoices(list);
}

void ElevenLabsTtsEngine::addCustomVoice(const QString &id, const QString &name)
{
    CloudTts::addCustomVoice(m_ctx.settings, Keys::ElevenLabsCustomVoices, id, name);
    publishVoices();
}

void ElevenLabsTtsEngine::removeCustomVoice(const QString &id)
{
    CloudTts::removeCustomVoice(m_ctx.settings, Keys::ElevenLabsCustomVoices, id);
    publishVoices();
}

TtsStream *ElevenLabsTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    if (!isAvailable()) {
        auto *stream = new BufferTtsStream;
        stream->deliverFailedLater(unavailableReason());
        return stream;
    }
    QString model = m_ctx.settings ? m_ctx.settings->string(Keys::ElevenLabsModel).trimmed() : QString();
    if (model.isEmpty())
        model = QStringLiteral("eleven_flash_v2_5");

    const QJsonObject voiceSettings{
        {QStringLiteral("stability"), 0.5},
        {QStringLiteral("similarity_boost"), 0.75},
        {QStringLiteral("speed"), qBound(0.7, options.rate, 1.2)},
    };
    const QJsonObject body{
        {QStringLiteral("text"), text},
        {QStringLiteral("model_id"), model},
        {QStringLiteral("voice_settings"), voiceSettings},
    };
    QNetworkRequest req = request(QStringLiteral("/v1/text-to-speech/%1/stream?output_format=pcm_24000")
                                      .arg(QString::fromLatin1(QUrl::toPercentEncoding(voice.id))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_ctx.network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    return new NetworkTtsStream(reply, PcmStreamParser::Container::Raw, AudioConvert::int16Mono(24000),
                                displayName());
}
