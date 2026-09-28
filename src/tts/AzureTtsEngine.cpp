#include "tts/AzureTtsEngine.h"

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

#include <algorithm>

namespace {

QString xmlEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&':
            out += QLatin1String("&amp;");
            break;
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '\'':
            out += QLatin1String("&apos;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        default:
            out += c;
        }
    }
    return out;
}

QString withSign(int value)
{
    return (value > 0 ? QStringLiteral("+") : QString()) + QString::number(value);
}

} // namespace

AzureTtsEngine::AzureTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
{
}

bool AzureTtsEngine::isAvailable() const
{
    return m_ctx.secrets && m_ctx.secrets->has(Secrets::Azure);
}

QString AzureTtsEngine::unavailableReason() const
{
    return isAvailable() ? QString() : tr("Add your Microsoft Azure Speech key in Settings → Voices.");
}

QString AzureTtsEngine::region() const
{
    const QString r = m_ctx.settings ? m_ctx.settings->string(Keys::AzureRegion).trimmed().toLower() : QString();
    return r.isEmpty() ? QStringLiteral("eastus") : r;
}

QNetworkRequest AzureTtsEngine::request(const QString &path) const
{
    const QString base = baseUrl(QStringLiteral("https://%1.tts.speech.microsoft.com").arg(region()));
    QNetworkRequest req = NetworkUtil::jsonRequest(QUrl(base + path));
    req.setRawHeader("Ocp-Apim-Subscription-Key", m_ctx.secrets->get(Secrets::Azure).toUtf8());
    return req;
}

QString AzureTtsEngine::buildSsml(const QString &text, const QString &voiceName, const QString &locale,
                                  const SpeakOptions &options)
{
    QString prosody;
    const int ratePercent = qRound((qBound(0.5, options.rate, 2.0) - 1.0) * 100.0);
    if (ratePercent != 0)
        prosody += QStringLiteral(" rate='%1%'").arg(withSign(ratePercent));
    const int semitones = qRound(qBound(-1.0, options.pitch, 1.0) * 6.0);
    if (semitones != 0)
        prosody += QStringLiteral(" pitch='%1st'").arg(withSign(semitones));

    QString body = xmlEscape(text);
    if (!prosody.isEmpty())
        body = QStringLiteral("<prosody%1>%2</prosody>").arg(prosody, body);
    return QStringLiteral("<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='%1'>"
                          "<voice name='%2'>%3</voice></speak>")
        .arg(xmlEscape(locale), xmlEscape(voiceName), body);
}

void AzureTtsEngine::refreshVoices()
{
    if (!isAvailable()) {
        setVoices({});
        return;
    }
    setRefreshing(true);
    const int generation = ++m_refreshGeneration;
    CloudTts::getJson(m_ctx.network, request(QStringLiteral("/cognitiveservices/voices/list")), displayName(), this,
                      [this, generation](const QJsonDocument &json, const QString &error) {
        if (generation != m_refreshGeneration)
            return;
        if (!error.isEmpty()) {
            setRefreshing(false);
            emit voicesError(error);
            return;
        }
        QList<Voice> list;
        const QJsonArray arr = json.array();
        for (const QJsonValue &value : arr) {
            const QJsonObject o = value.toObject();
            Voice v;
            v.engineId = id();
            v.id = o.value(QStringLiteral("ShortName")).toString();
            if (v.id.isEmpty())
                continue;
            const QString display = o.value(QStringLiteral("DisplayName")).toString();
            const QString local = o.value(QStringLiteral("LocalName")).toString();
            v.name = display.isEmpty() ? v.id : display;
            if (!local.isEmpty() && local != v.name)
                v.name += QStringLiteral(" (%1)").arg(local);
            v.language = o.value(QStringLiteral("Locale")).toString();
            v.gender = CloudTts::normalizeGender(o.value(QStringLiteral("Gender")).toString());

            QStringList details;
            const QString type = o.value(QStringLiteral("VoiceType")).toString();
            if (!type.isEmpty() && type != QLatin1String("Neural"))
                details << type;
            QStringList styles;
            const QJsonArray styleList = o.value(QStringLiteral("StyleList")).toArray();
            for (const QJsonValue &s : styleList)
                styles << s.toString();
            styles.removeAll(QString());
            if (!styles.isEmpty())
                details << tr("Styles: %1").arg(styles.join(QStringLiteral(", ")));
            v.description = details.join(QStringLiteral(" · "));
            list << v;
        }
        std::sort(list.begin(), list.end(), [](const Voice &a, const Voice &b) {
            const int byLocale = a.language.compare(b.language, Qt::CaseInsensitive);
            if (byLocale != 0)
                return byLocale < 0;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        setVoices(list);
    });
}

TtsStream *AzureTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    if (!isAvailable()) {
        auto *stream = new BufferTtsStream;
        stream->deliverFailedLater(unavailableReason());
        return stream;
    }
    // "en-US-JennyNeural" -> "en-US" when the voice list isn't loaded yet.
    QString locale = voice.language;
    if (locale.isEmpty())
        locale = voice.id.section(QLatin1Char('-'), 0, 1);

    QNetworkRequest req = request(QStringLiteral("/cognitiveservices/v1"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/ssml+xml"));
    req.setRawHeader("X-Microsoft-OutputFormat", "raw-24khz-16bit-mono-pcm");
    QNetworkReply *reply = m_ctx.network->post(req, buildSsml(text, voice.id, locale, options).toUtf8());
    return new NetworkTtsStream(reply, PcmStreamParser::Container::Raw, AudioConvert::int16Mono(24000),
                                displayName());
}
