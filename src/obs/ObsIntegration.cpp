#include "obs/ObsIntegration.h"

#include <QTimer>

// Placeholder implementation (replaced by the OBS work package).
ObsIntegration::ObsIntegration(QObject *parent) : QObject(parent) {}
ObsIntegration::~ObsIntegration() = default;
void ObsIntegration::setConfig(const Config &config) { m_config = config; }
void ObsIntegration::utteranceStarted(const QString &) {}
void ObsIntegration::utteranceFinished(const QString &) {}
void ObsIntegration::listTextSources(ListCallback callback) { callback({}, tr("Not connected")); }
void ObsIntegration::listAllSources(ListCallback callback) { callback({}, tr("Not connected")); }
void ObsIntegration::createTextSource(const QString &, DoneCallback callback) { callback(false, tr("Not connected")); }
void ObsIntegration::addBrowserOverlay(const QString &, const QUrl &, DoneCallback callback) { callback(false, tr("Not connected")); }
void ObsIntegration::testSubtitle(DoneCallback callback) { callback(false, tr("Not connected")); }
void ObsIntegration::reconnect() {}
void ObsIntegration::setStatus(Status status, const QString &text) { m_status = status; m_statusText = text; emit statusChanged(status, text); }
void ObsIntegration::setSubtitleText(const QString &) {}
void ObsIntegration::setIndicator(bool) {}
