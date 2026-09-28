#include "core/UpdateChecker.h"

#include <QTimer>

// Placeholder implementation (replaced by the text work package).
UpdateChecker::UpdateChecker(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network),
      m_apiUrl(QStringLiteral("https://api.github.com/repos/Vocal-Ink/Desktop-app/releases/latest")) {}
void UpdateChecker::check() { QTimer::singleShot(0, this, &UpdateChecker::upToDate); }
void UpdateChecker::setApiUrl(const QString &url) { m_apiUrl = url; }
bool UpdateChecker::isNewer(const QString &, const QString &) { return false; }
