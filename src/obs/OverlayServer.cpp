#include "obs/OverlayServer.h"

// Placeholder implementation (replaced by the OBS work package).
OverlayServer::OverlayServer(QObject *parent) : QObject(parent) {}
OverlayServer::~OverlayServer() = default;
bool OverlayServer::start(quint16, bool) { m_error = tr("Not implemented"); return false; }
void OverlayServer::stop() {}
bool OverlayServer::isRunning() const { return false; }
quint16 OverlayServer::port() const { return 0; }
QUrl OverlayServer::overlayUrl(const QString &) const { return {}; }
void OverlayServer::showCaption(quint64, const QString &, const QString &) {}
void OverlayServer::endCaption(quint64) {}
void OverlayServer::clearCaptions() {}
void OverlayServer::setSpeaking(bool) {}
void OverlayServer::setListening(bool) {}
void OverlayServer::onNewConnection() {}
void OverlayServer::handleHttp(QTcpSocket *) {}
void OverlayServer::broadcast(const QByteArray &) {}
