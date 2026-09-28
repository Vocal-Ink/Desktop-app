#include "tts/FishAudioTtsEngine.h"

#include "core/NetworkUtil.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "tts/CloudHelpers.h"
#include "tts/StreamHelpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

#include <memory>

FishAudioTtsEngine::FishAudioTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
    // Pasted voice ids are usable before the first refresh.
    m_voices = CloudTts::customVoices(m_ctx.settings, Keys::FishCustomVoices, id());
}

bool FishAudioTtsEngine::isAvailable() const
{
    return m_ctx.secrets && m_ctx.secrets->has(Secrets::FishAudio);
}

QString FishAudioTtsEngine::unavailableReason() const
{
    return isAvailable() ? QString() : tr("Add your Fish Audio API key in Settings → Voice providers.");
}

QNetworkRequest FishAudioTtsEngine::request(const QString &pathAndQuery) const
{
    const QString base = baseUrl(QStringLiteral("https://api.fish.audio"));
    QNetworkRequest req = NetworkUtil::jsonRequest(QUrl(base + pathAndQuery));
    req.setRawHeader("Authorization", "Bearer " + m_ctx.secrets->get(Secrets::FishAudio).toUtf8());
    return req;
}

QList<Voice> FishAudioTtsEngine::parseModels(const QJsonDocument &json, bool own) const
{
    QList<Voice> list;
    const QJsonArray items = json.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject o = value.toObject();
        Voice v;
        v.engineId = id();
        v.id = o.value(QStringLiteral("_id")).toString();
        if (v.id.isEmpty())
            continue;
        v.name = o.value(QStringLiteral("title")).toString().simplified();
        if (v.name.isEmpty())
            v.name = v.id;
        const QJsonArray languages = o.value(QStringLiteral("languages")).toArray();
        if (!languages.isEmpty())
            v.language = languages.first().toString();

        QStringList tags;
        const QJsonArray tagArray = o.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue &t : tagArray) {
            const QString tag = t.toString().simplified();
            if (tag.isEmpty())
                continue;
            tags << tag;
            if (v.gender.isEmpty())
                v.gender = CloudTts::normalizeGender(tag);
        }
        QString description = tags.join(QStringLiteral(", "));
        if (description.isEmpty()) {
            description = o.value(QStringLiteral("description")).toString().simplified();
            if (description.size() > 120)
                description = description.left(117) + QStringLiteral("...");
        }
        if (own)
            description = description.isEmpty() ? tr("Your model") : tr("Your model") + QStringLiteral(" · ") + description;
        v.description = description;

        const QJsonArray samples = o.value(QStringLiteral("samples")).toArray();
        if (!samples.isEmpty())
            v.previewUrl = samples.first().toObject().value(QStringLiteral("audio")).toString();
        list << v;
    }
    return list;
}

void FishAudioTtsEngine::refreshVoices()
{
    if (!isAvailable()) {
        m_own.clear();
        m_popular.clear();
        publishVoices();
        return;
    }
    setRefreshing(true);
    const int generation = ++m_refreshGeneration;

    struct Pending
    {
        int left = 2;
        bool anyOk = false;
        QString error;
        QList<Voice> own;
        QList<Voice> popular;
    };
    auto pending = std::make_shared<Pending>();
    auto done = [this, generation, pending] {
        if (--pending->left > 0 || generation != m_refreshGeneration)
            return;
        if (!pending->anyOk) {
            setRefreshing(false);
            emit voicesError(pending->error);
            return;
        }
        m_own = pending->own;
        m_popular = pending->popular;
        publishVoices();
    };

    CloudTts::getJson(m_ctx.network, request(QStringLiteral("/model?self=true&page_size=100")), displayName(), this,
                      [this, pending, done](const QJsonDocument &json, const QString &error) {
        if (error.isEmpty()) {
            pending->own = parseModels(json, true);
            pending->anyOk = true;
        } else if (pending->error.isEmpty()) {
            pending->error = error;
        }
        done();
    });
    CloudTts::getJson(m_ctx.network, request(QStringLiteral("/model?page_size=50&sort_by=task_count")), displayName(),
                      this, [this, pending, done](const QJsonDocument &json, const QString &error) {
        if (error.isEmpty()) {
            pending->popular = parseModels(json, false);
            pending->anyOk = true;
        } else if (pending->error.isEmpty()) {
            pending->error = error;
        }
        done();
    });
}

void FishAudioTtsEngine::publishVoices()
{
    // Pasted ids first, then the user's own models, then popular ones.
    QList<Voice> list;
    const QList<Voice> custom = CloudTts::customVoices(m_ctx.settings, Keys::FishCustomVoices, id());
    for (const Voice &v : custom) {
        if (!m_own.contains(v) && !m_popular.contains(v))
            list << v;
    }
    CloudTts::mergeVoices(list, m_own);
    CloudTts::mergeVoices(list, m_popular);
    setVoices(list);
}

void FishAudioTtsEngine::searchVoices(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty() || !isAvailable()) {
        QTimer::singleShot(0, this, [this, query] { emit searchFinished(query, {}); });
        return;
    }
    const QString path = QStringLiteral("/model?title=%1&page_size=30&sort_by=task_count")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(trimmed)));
    CloudTts::getJson(m_ctx.network, request(path), displayName(), this,
                      [this, query](const QJsonDocument &json, const QString &error) {
        if (!error.isEmpty()) {
            emit voicesError(error);
            emit searchFinished(query, {});
            return;
        }
        emit searchFinished(query, parseModels(json, false));
    });
}

void FishAudioTtsEngine::addCustomVoice(const QString &id, const QString &name)
{
    CloudTts::addCustomVoice(m_ctx.settings, Keys::FishCustomVoices, id, name);
    publishVoices();
}

void FishAudioTtsEngine::removeCustomVoice(const QString &id)
{
    CloudTts::removeCustomVoice(m_ctx.settings, Keys::FishCustomVoices, id);
    publishVoices();
}

TtsStream *FishAudioTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    if (!isAvailable()) {
        auto *stream = new BufferTtsStream;
        stream->deliverFailedLater(unavailableReason());
        return stream;
    }
    QString model = m_ctx.settings ? m_ctx.settings->string(Keys::FishModel).trimmed() : QString();
    if (model.isEmpty())
        model = QStringLiteral("s1");

    const QJsonObject body{
        {QStringLiteral("text"), text},
        {QStringLiteral("reference_id"), voice.id},
        {QStringLiteral("format"), QStringLiteral("wav")},
        {QStringLiteral("sample_rate"), 24000},
        {QStringLiteral("latency"), QStringLiteral("balanced")},
        {QStringLiteral("normalize"), true},
        {QStringLiteral("prosody"), QJsonObject{{QStringLiteral("speed"), qBound(0.5, options.rate, 2.0)}}},
    };
    QNetworkRequest req = request(QStringLiteral("/v1/tts"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    req.setRawHeader("model", model.toUtf8());
    QNetworkReply *reply = m_ctx.network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    return new NetworkTtsStream(reply, PcmStreamParser::Container::Wav, QAudioFormat(), displayName());
}
