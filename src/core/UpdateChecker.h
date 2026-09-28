#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

// Asks GitHub for the latest release and reports when it is newer than this build.
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    UpdateChecker(QNetworkAccessManager *network, QObject *parent = nullptr);

    void check();                       // async
    void setApiUrl(const QString &url); // tests
    static bool isNewer(const QString &candidate, const QString &current); // "v1.2.10" > "1.2.9"

signals:
    void updateAvailable(const QString &version, const QUrl &downloadPage, const QString &notes);
    void upToDate();
    void failed(const QString &message);

private:
    QNetworkAccessManager *m_network;
    QString m_apiUrl;
};
