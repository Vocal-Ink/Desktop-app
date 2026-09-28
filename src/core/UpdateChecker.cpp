#include "core/UpdateChecker.h"

#include "Version.h"
#include "core/NetworkUtil.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>

namespace {

constexpr int kTimeoutMs = 15000;

struct ParsedVersion
{
    QList<qint64> numbers;  // 1.2.10 -> {1, 2, 10}
    QStringList preRelease; // "beta.2" -> {"beta", "2"}
};

bool parseVersion(QString text, ParsedVersion *out)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        text.remove(0, 1);
    const qsizetype plus = text.indexOf(QLatin1Char('+')); // build metadata doesn't count
    if (plus >= 0)
        text.truncate(plus);
    QString pre;
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        pre = text.mid(dash + 1);
        text.truncate(dash);
    }
    const QStringList parts = text.split(QLatin1Char('.'));
    for (const QString &part : parts) {
        qsizetype digits = 0;
        while (digits < part.size() && part.at(digits).isDigit())
            ++digits;
        if (digits == 0)
            break;
        out->numbers << part.left(digits).toLongLong();
        if (digits < part.size()) { // "2rc1": the rest is a pre-release tag
            if (pre.isEmpty())
                pre = part.mid(digits);
            break;
        }
    }
    out->preRelease = pre.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    return !out->numbers.isEmpty();
}

int compareIdentifiers(const QString &a, const QString &b)
{
    bool aNumeric = false;
    bool bNumeric = false;
    const qint64 an = a.toLongLong(&aNumeric);
    const qint64 bn = b.toLongLong(&bNumeric);
    if (aNumeric && bNumeric)
        return an < bn ? -1 : (an > bn ? 1 : 0);
    if (aNumeric != bNumeric)
        return aNumeric ? -1 : 1; // numeric identifiers sort before words
    return a.compare(b, Qt::CaseInsensitive);
}

int compareVersions(const ParsedVersion &a, const ParsedVersion &b)
{
    const qsizetype n = qMax(a.numbers.size(), b.numbers.size());
    for (qsizetype i = 0; i < n; ++i) {
        const qint64 x = a.numbers.value(i, 0);
        const qint64 y = b.numbers.value(i, 0);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a.preRelease.isEmpty() || b.preRelease.isEmpty())
        return int(a.preRelease.isEmpty()) - int(b.preRelease.isEmpty()); // a release beats its betas
    const qsizetype m = qMin(a.preRelease.size(), b.preRelease.size());
    for (qsizetype i = 0; i < m; ++i) {
        const int c = compareIdentifiers(a.preRelease.at(i), b.preRelease.at(i));
        if (c != 0)
            return c < 0 ? -1 : 1;
    }
    if (a.preRelease.size() == b.preRelease.size())
        return 0;
    return a.preRelease.size() < b.preRelease.size() ? -1 : 1;
}

} // namespace

UpdateChecker::UpdateChecker(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_apiUrl(QStringLiteral("https://api.github.com/repos/Vocal-Ink/Desktop-app/releases/latest"))
{
}

void UpdateChecker::setApiUrl(const QString &url)
{
    m_apiUrl = url;
}

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    ParsedVersion a;
    ParsedVersion b;
    if (!parseVersion(candidate, &a) || !parseVersion(current, &b))
        return false;
    return compareVersions(a, b) > 0;
}

void UpdateChecker::check()
{
    if (m_reply || !m_network)
        return;
    QNetworkRequest request = NetworkUtil::jsonRequest(QUrl(m_apiUrl), kTimeoutMs);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        onFinished(reply);
    });
}

void UpdateChecker::onFinished(QNetworkReply *reply)
{
    const int status = NetworkUtil::httpStatus(reply);
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        if (status == 404)
            emit failed(tr("Couldn't check for updates: no published release was found."));
        else if (status == 403 || status == 429)
            emit failed(tr("GitHub is limiting update checks right now. Please try again later."));
        else if (status >= 500)
            emit failed(tr("GitHub is having problems right now. Please try again later."));
        else if (status > 0)
            emit failed(tr("Couldn't check for updates (GitHub answered with HTTP %1).").arg(status));
        else
            emit failed(tr("Couldn't check for updates. Check your internet connection and try again."));
        return;
    }

    const QJsonObject release = QJsonDocument::fromJson(body).object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString().trimmed();
    ParsedVersion parsed;
    if (tag.isEmpty() || !parseVersion(tag, &parsed)) {
        emit failed(tr("Couldn't check for updates: GitHub sent an unexpected answer."));
        return;
    }
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()
        || !isNewer(tag, QStringLiteral(VOCALINK_VERSION))) {
        emit upToDate();
        return;
    }

    QUrl page(release.value(QStringLiteral("html_url")).toString());
    if (!page.isValid() || (page.scheme() != QLatin1String("https") && page.scheme() != QLatin1String("http")))
        page = QUrl(QStringLiteral(VOCALINK_HOMEPAGE "/releases/latest"));
    const QString version = tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? tag.mid(1) : tag;
    emit updateAvailable(version, page, release.value(QStringLiteral("body")).toString().trimmed());
}
