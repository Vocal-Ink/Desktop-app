#include "core/NetworkUtil.h"

#include "Version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace NetworkUtil {

QByteArray userAgent()
{
    return QByteArrayLiteral("VocalInk/") + QByteArray(VOCALINK_VERSION);
}

QNetworkRequest jsonRequest(const QUrl &url, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

int httpStatus(QNetworkReply *reply)
{
    return reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
}

static QString messageFromJson(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};
    const QJsonObject o = doc.object();
    auto str = [](const QJsonValue &v) -> QString {
        if (v.isString())
            return v.toString();
        if (v.isObject()) {
            const QJsonObject inner = v.toObject();
            for (const auto *k : {"message", "msg", "detail", "error"}) {
                const QJsonValue x = inner.value(QLatin1String(k));
                if (x.isString())
                    return x.toString();
            }
        }
        return {};
    };
    for (const auto *k : {"error", "detail", "message", "msg"}) {
        const QString s = str(o.value(QLatin1String(k)));
        if (!s.isEmpty())
            return s;
    }
    return {};
}

QString describeError(QNetworkReply *reply, const QByteArray &body, const QString &provider)
{
    const int status = httpStatus(reply);
    QString detail = messageFromJson(body);
    if (detail.isEmpty() && !body.isEmpty() && body.size() < 300 && !body.contains('\0'))
        detail = QString::fromUtf8(body).trimmed();

    QString base;
    switch (status) {
    case 400:
        base = QObject::tr("%1 rejected the request").arg(provider);
        break;
    case 401:
    case 403:
        base = QObject::tr("%1 did not accept the API key. Check it in Settings → Voices").arg(provider);
        break;
    case 402:
        base = QObject::tr("%1 says the account is out of credit or needs a paid plan").arg(provider);
        break;
    case 404:
        base = QObject::tr("%1 could not find that voice or model").arg(provider);
        break;
    case 429:
        base = QObject::tr("%1 rate limit or quota reached; wait a moment and try again").arg(provider);
        break;
    default:
        if (status >= 500)
            base = QObject::tr("%1 is having problems (HTTP %2)").arg(provider).arg(status);
        else if (status > 0)
            base = QObject::tr("%1 returned HTTP %2").arg(provider).arg(status);
        else if (reply && reply->error() == QNetworkReply::OperationCanceledError)
            base = QObject::tr("%1 took too long to answer").arg(provider);
        else
            base = QObject::tr("Could not reach %1: %2").arg(provider, reply ? reply->errorString() : QString());
        break;
    }
    return detail.isEmpty() ? base : base + QStringLiteral(" (") + detail + QLatin1Char(')');
}

} // namespace NetworkUtil
