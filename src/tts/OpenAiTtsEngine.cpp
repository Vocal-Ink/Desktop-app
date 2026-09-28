#include "tts/OpenAiTtsEngine.h"

#include "audio/AudioConvert.h"
#include "core/NetworkUtil.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "tts/StreamHelpers.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace {
const QString kDefaultBase = QStringLiteral("https://api.openai.com/v1");

QString trimSlashes(QString url)
{
    url = url.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}
} // namespace

OpenAiTtsEngine::OpenAiTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
    m_voices = builtInVoices();
    if (m_ctx.settings) {
        connect(m_ctx.settings, &Settings::changed, this, [this](const QString &key) {
            if (key == QLatin1String(Keys::OpenAiBaseUrl))
                emit availabilityChanged();
        });
    }
}

QList<Voice> OpenAiTtsEngine::builtInVoices()
{
    const QStringList ids = {
        QStringLiteral("alloy"), QStringLiteral("ash"),  QStringLiteral("ballad"),  QStringLiteral("coral"),
        QStringLiteral("echo"),  QStringLiteral("fable"), QStringLiteral("nova"),   QStringLiteral("onyx"),
        QStringLiteral("sage"),  QStringLiteral("shimmer"), QStringLiteral("verse"), QStringLiteral("marin"),
        QStringLiteral("cedar"),
    };
    QList<Voice> list;
    for (const QString &voiceId : ids) {
        Voice v;
        v.engineId = QStringLiteral("openai");
        v.id = voiceId;
        v.name = voiceId.left(1).toUpper() + voiceId.mid(1);
        // Every voice speaks every supported language, so language stays empty.
        if (voiceId == QLatin1String("marin") || voiceId == QLatin1String("cedar"))
            v.description = tr("Recommended");
        list << v;
    }
    return list;
}

QString OpenAiTtsEngine::apiBase() const
{
    const QString configured = m_ctx.settings ? trimSlashes(m_ctx.settings->string(Keys::OpenAiBaseUrl)) : QString();
    return trimSlashes(baseUrl(configured.isEmpty() ? kDefaultBase : configured));
}

bool OpenAiTtsEngine::usesCustomServer() const
{
    const QString configured = m_ctx.settings ? trimSlashes(m_ctx.settings->string(Keys::OpenAiBaseUrl)) : QString();
    return !configured.isEmpty() && configured != kDefaultBase;
}

bool OpenAiTtsEngine::isAvailable() const
{
    // Self-hosted OpenAI-compatible servers often need no key.
    return (m_ctx.secrets && m_ctx.secrets->has(Secrets::OpenAi)) || usesCustomServer();
}

QString OpenAiTtsEngine::unavailableReason() const
{
    return isAvailable() ? QString() : tr("Add your OpenAI API key in Settings → Voice providers.");
}

void OpenAiTtsEngine::refreshVoices()
{
    setVoices(builtInVoices());
}

TtsStream *OpenAiTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    if (!isAvailable()) {
        auto *stream = new BufferTtsStream;
        stream->deliverFailedLater(unavailableReason());
        return stream;
    }
    QString model = m_ctx.settings ? m_ctx.settings->string(Keys::OpenAiTtsModel).trimmed() : QString();
    if (model.isEmpty())
        model = QStringLiteral("gpt-4o-mini-tts");

    QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("input"), text},
        {QStringLiteral("voice"), voice.id},
        {QStringLiteral("response_format"), QStringLiteral("pcm")},
        {QStringLiteral("speed"), qBound(0.25, options.rate, 4.0)},
    };
    // tts-1 and tts-1-hd reject "instructions".
    const QString instructions = options.instructions.trimmed();
    if (!instructions.isEmpty() && !model.startsWith(QLatin1String("tts-1")))
        body.insert(QStringLiteral("instructions"), instructions);

    QNetworkRequest req = NetworkUtil::jsonRequest(QUrl(apiBase() + QStringLiteral("/audio/speech")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QString key = m_ctx.secrets ? m_ctx.secrets->get(Secrets::OpenAi) : QString();
    if (!key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    QNetworkReply *reply = m_ctx.network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    return new NetworkTtsStream(reply, PcmStreamParser::Container::Raw, AudioConvert::int16Mono(24000),
                                displayName());
}
