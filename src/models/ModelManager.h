#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

struct WhisperModelInfo
{
    QString file;        // e.g. "ggml-base.en-q5_1.bin"
    QString title;       // e.g. "Base (English)"
    QString description; // one line: speed/accuracy trade-off
    qint64 approxBytes = 0;
    bool multilingual = false;
    bool recommended = false;
};

struct PiperVoiceInfo
{
    struct File
    {
        QString relPath; // path inside the piper-voices repository
        qint64 size = 0;
        QString md5;
    };
    QString key;          // e.g. "en_US-lessac-medium"
    QString name;         // e.g. "lessac"
    QString languageCode; // e.g. "en_US"
    QString languageName; // e.g. "English (United States)"
    QString quality;      // x_low | low | medium | high
    int numSpeakers = 1;
    QList<File> files;
    qint64 totalBytes() const;
};

// Downloads and tracks the local models: Whisper speech-recognition models, the
// Piper runtime and Piper voices. Task ids used in signals:
//   "whisper:<file>", "piper-runtime", "piper-voice:<key>".
class ModelManager : public QObject
{
    Q_OBJECT
public:
    explicit ModelManager(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ModelManager() override;

    // --- Whisper ---
    static QList<WhisperModelInfo> whisperCatalog();
    QStringList installedWhisperModels() const; // file names
    bool isWhisperModelInstalled(const QString &file) const;
    static QString whisperModelPath(const QString &file);
    void downloadWhisperModel(const QString &file);
    bool removeWhisperModel(const QString &file);

    // --- Piper runtime ---
    static QUrl piperRuntimeUrl();              // empty if there is no build for this platform
    static QString bundledPiperExecutable();    // where the downloaded runtime puts piper(.exe)
    bool isPiperRuntimeInstalled() const;
    void downloadPiperRuntime();

    // --- Piper voices ---
    void refreshPiperCatalog();                 // fetches voices.json (cached on disk)
    QList<PiperVoiceInfo> piperCatalog() const { return m_piperCatalog; }
    bool isPiperVoiceInstalled(const QString &key) const;
    static QString piperVoiceModelPath(const QString &key); // .../<key>/<key>.onnx
    void downloadPiperVoice(const QString &key);
    bool removePiperVoice(const QString &key);
    static QString recommendedPiperVoice();     // "en_US-lessac-medium"

    // --- Download control ---
    bool isDownloading(const QString &taskId) const;
    void cancel(const QString &taskId);

    // Test hooks
    void setWhisperBaseUrl(const QString &url) { m_whisperBase = url; }
    void setPiperVoicesBaseUrl(const QString &url) { m_piperVoicesBase = url; }
    void setPiperRuntimeUrlOverride(const QUrl &url) { m_runtimeOverride = url; }

signals:
    void downloadProgress(const QString &taskId, qint64 received, qint64 total);
    void downloadFinished(const QString &taskId);
    void downloadFailed(const QString &taskId, const QString &error);
    void piperCatalogChanged();
    void piperCatalogError(const QString &message);
    void installedChanged();

private:
    class Private;
    Private *d;
    QNetworkAccessManager *m_network;
    QList<PiperVoiceInfo> m_piperCatalog;
    QString m_whisperBase;
    QString m_piperVoicesBase;
    QUrl m_runtimeOverride;
};
