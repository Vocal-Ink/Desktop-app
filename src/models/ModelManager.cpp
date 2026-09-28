#include "models/ModelManager.h"

#include "core/Paths.h"

#include <QFileInfo>

// Placeholder implementation (replaced by the STT/models work package).
class ModelManager::Private {};

qint64 PiperVoiceInfo::totalBytes() const
{
    qint64 t = 0;
    for (const File &f : files)
        t += f.size;
    return t;
}

ModelManager::ModelManager(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), d(new Private), m_network(network) {}
ModelManager::~ModelManager() { delete d; }
QList<WhisperModelInfo> ModelManager::whisperCatalog() { return {}; }
QStringList ModelManager::installedWhisperModels() const { return {}; }
bool ModelManager::isWhisperModelInstalled(const QString &file) const { return QFileInfo::exists(whisperModelPath(file)); }
QString ModelManager::whisperModelPath(const QString &file) { return Paths::whisperModelsDir() + QLatin1Char('/') + file; }
void ModelManager::downloadWhisperModel(const QString &file) { emit downloadFailed(QStringLiteral("whisper:") + file, tr("Not implemented")); }
bool ModelManager::removeWhisperModel(const QString &) { return false; }
QUrl ModelManager::piperRuntimeUrl() { return {}; }
QString ModelManager::bundledPiperExecutable() { return {}; }
bool ModelManager::isPiperRuntimeInstalled() const { return false; }
void ModelManager::downloadPiperRuntime() {}
void ModelManager::refreshPiperCatalog() {}
bool ModelManager::isPiperVoiceInstalled(const QString &) const { return false; }
QString ModelManager::piperVoiceModelPath(const QString &key) { return Paths::piperVoicesDir() + QLatin1Char('/') + key + QLatin1Char('/') + key + QStringLiteral(".onnx"); }
void ModelManager::downloadPiperVoice(const QString &) {}
bool ModelManager::removePiperVoice(const QString &) { return false; }
QString ModelManager::recommendedPiperVoice() { return QStringLiteral("en_US-lessac-medium"); }
bool ModelManager::isDownloading(const QString &) const { return false; }
void ModelManager::cancel(const QString &) {}
