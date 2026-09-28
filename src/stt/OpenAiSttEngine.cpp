#include "stt/OpenAiSttEngine.h"

#include "audio/AudioConvert.h"
#include "core/NetworkUtil.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "core/TextProcessor.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>

namespace {

constexpr int kTimeoutMs = 120000;

QHttpPart formField(const QString &name, const QByteArray &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value);
    return part;
}

// "en", "en-US", "EN_us" -> "en"; empty for auto-detect.
QString languageCode(const QString &language)
{
    QString code = language.trimmed().toLower();
    const qsizetype sep = code.indexOf(QRegularExpression(QStringLiteral("[-_]")));
    if (sep > 0)
        code.truncate(sep);
    return code == QLatin1String("auto") ? QString() : code;
}

} // namespace

OpenAiSttEngine::OpenAiSttEngine(const EngineContext &context, QObject *parent)
    : SttEngine(parent)
    , m_ctx(context)
{
    m_lastReady = isReady();
    if (m_ctx.secrets) {
        const auto update = [this] {
            const bool ready = isReady();
            if (ready != m_lastReady) {
                m_lastReady = ready;
                emit readyChanged(ready);
            }
        };
        connect(m_ctx.secrets, &SecretStore::changed, this, update);
        connect(m_ctx.secrets, &SecretStore::loaded, this, update);
    }
}

OpenAiSttEngine::~OpenAiSttEngine()
{
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_replies)) {
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
}

QString OpenAiSttEngine::apiKey() const
{
    if (!m_ctx.secrets)
        return {};
    const QString own = m_ctx.secrets->get(Secrets::OpenAiStt);
    return own.isEmpty() ? m_ctx.secrets->get(Secrets::OpenAi) : own;
}

QString OpenAiSttEngine::baseUrl() const
{
    QString url = m_ctx.settings ? m_ctx.settings->string(Keys::SttOpenAiBaseUrl).trimmed() : QString();
    if (url.isEmpty())
        url = Settings::defaultValue(Keys::SttOpenAiBaseUrl).toString();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString OpenAiSttEngine::providerName() const
{
    const QString host = QUrl(baseUrl()).host();
    if (host.isEmpty() || host.endsWith(QLatin1String("openai.com")))
        return QStringLiteral("OpenAI");
    return host;
}

bool OpenAiSttEngine::isReady() const
{
    return !apiKey().isEmpty();
}

QString OpenAiSttEngine::notReadyReason() const
{
    if (isReady())
        return {};
    return tr("Add your OpenAI API key in Settings → Speech recognition to use cloud speech recognition.");
}

void OpenAiSttEngine::failLater(quint64 requestId, const QString &error)
{
    QTimer::singleShot(0, this, [this, requestId, error] { emit failed(requestId, error); });
}

void OpenAiSttEngine::transcribe(quint64 requestId, const QVector<float> &mono16k)
{
    if (!isReady()) {
        failLater(requestId, notReadyReason());
        return;
    }
    if (!m_ctx.network) {
        failLater(requestId, tr("Cloud speech recognition is not available (no network access)."));
        return;
    }

    QString model = m_ctx.settings ? m_ctx.settings->string(Keys::SttOpenAiModel).trimmed() : QString();
    if (model.isEmpty())
        model = Settings::defaultValue(Keys::SttOpenAiModel).toString();

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"speech.wav\""));
    file.setBody(AudioConvert::makeWav16(mono16k, 16000));
    multi->append(file);
    multi->append(formField(QStringLiteral("model"), model.toUtf8()));
    const QString language = languageCode(m_options.language);
    if (!language.isEmpty())
        multi->append(formField(QStringLiteral("language"), language.toUtf8()));
    const QString prompt = m_options.prompt.trimmed();
    if (!prompt.isEmpty())
        multi->append(formField(QStringLiteral("prompt"), prompt.toUtf8()));
    multi->append(formField(QStringLiteral("response_format"), QByteArrayLiteral("json")));

    QNetworkRequest req = NetworkUtil::jsonRequest(QUrl(baseUrl() + QStringLiteral("/audio/transcriptions")), kTimeoutMs);
    req.setRawHeader("Authorization", "Bearer " + apiKey().toUtf8());
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_ctx.network->post(req, multi);
    multi->setParent(reply);
    m_replies.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this, [this, requestId, reply] { onFinished(requestId, reply); });
}

void OpenAiSttEngine::onFinished(quint64 requestId, QNetworkReply *reply)
{
    m_replies.remove(requestId);
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || NetworkUtil::httpStatus(reply) >= 300) {
        emit failed(requestId, NetworkUtil::describeError(reply, body, providerName()));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QString text;
    if (doc.isObject() && doc.object().value(QLatin1String("text")).isString()) {
        text = doc.object().value(QLatin1String("text")).toString();
    } else if (doc.isNull() && reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("text/plain"))) {
        text = QString::fromUtf8(body); // servers that ignore response_format
    } else if (doc.isObject() && doc.object().contains(QLatin1String("error"))) {
        emit failed(requestId, NetworkUtil::describeError(reply, body, providerName()));
        return;
    } else {
        emit failed(requestId, tr("%1 sent an answer Vocal Ink does not understand.").arg(providerName()));
        return;
    }
    emit transcribed(requestId, TextProcessor::cleanTranscript(text));
}
