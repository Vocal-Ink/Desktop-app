#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Asks GitHub for the latest release and reports when it is newer than this build.
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    UpdateChecker(QNetworkAccessManager *network, QObject *parent = nullptr);

    void check();                       // async; ignored while a check is running
    bool isChecking() const { return !m_reply.isNull(); }
    void setApiUrl(const QString &url); // tests
    // Semantic comparison: "v1.2.10" > "1.2.9", "1.3.0" > "1.3.0-beta.2" > "1.3.0-beta.1".
    static bool isNewer(const QString &candidate, const QString &current);

signals:
    void updateAvailable(const QString &version, const QUrl &downloadPage, const QString &notes);
    void upToDate();
    void failed(const QString &message);

private:
    void onFinished(QNetworkReply *reply);

    QNetworkAccessManager *m_network;
    QString m_apiUrl;
    QPointer<QNetworkReply> m_reply;
};
