#include "audio/RoutingCheck.h"

#include <QTimer>

// Placeholder implementation (replaced by the audio work package).
class RoutingCheck::Private {};

RoutingCheck::RoutingCheck(QObject *parent) : QObject(parent), d(new Private) {}
RoutingCheck::~RoutingCheck() { delete d; }
void RoutingCheck::start(const QByteArray &, const QByteArray &, int)
{
    QTimer::singleShot(0, this, [this] { emit finished(false, tr("The routing check is not available in this build.")); });
}
void RoutingCheck::cancel() {}
bool RoutingCheck::isRunning() const { return false; }
QByteArray RoutingCheck::pairedInputFor(const QByteArray &) { return {}; }
