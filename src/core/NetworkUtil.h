#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>

class QNetworkReply;

namespace NetworkUtil {

QByteArray userAgent(); // "VocalInk/<version>"

// A request with our user agent, a transfer timeout and redirect following.
QNetworkRequest jsonRequest(const QUrl &url, int timeoutMs = 30000);

// Best-effort human-readable error for a failed provider call. Understands the
// common JSON error shapes ({"error":{"message"}}, {"detail":{"message"}},
// {"detail": "..."}, {"message": "..."}) and adds hints for 401/402/429.
QString describeError(QNetworkReply *reply, const QByteArray &body, const QString &provider);

int httpStatus(QNetworkReply *reply);

} // namespace NetworkUtil
