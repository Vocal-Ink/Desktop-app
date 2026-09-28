#pragma once

#include "stt/SttEngine.h"
#include "tts/TtsEngine.h" // EngineContext

#include <QDateTime>

class QFileSystemWatcher;
class QThread;
class QTimer;
class WhisperWorker;

// On-device recognition with whisper.cpp. The model (Keys::WhisperModel inside
// Paths::whisperModelsDir()) is loaded and run on a private worker thread, one
// request at a time; results are delivered on the thread that owns the engine.
class WhisperEngine : public SttEngine
{
    Q_OBJECT
public:
    explicit WhisperEngine(const EngineContext &context, QObject *parent = nullptr);
    ~WhisperEngine() override;

    QString id() const override { return QStringLiteral("whisper"); }
    QString displayName() const override { return tr("Whisper (on this device)"); }
    bool isLocal() const override { return true; }
    bool isReady() const override;
    QString notReadyReason() const override;

    void transcribe(quint64 requestId, const QVector<float> &mono16k) override;

    QString modelPath() const;
    bool isLoading() const { return m_loading; }

    // Test hook: load this model file instead of the one named in the settings.
    void setModelPathForTesting(const QString &path);

signals:
    void modelLoaded(const QString &path);
    void modelLoadFailed(const QString &path, const QString &error);

private:
    struct FileStamp
    {
        qint64 size = -1;
        QDateTime modified;
        bool operator==(const FileStamp &o) const { return size == o.size && modified == o.modified; }
    };
    static FileStamp stampOf(const QString &path);

    void reload();
    void onLoaded(const QString &path, bool ok, const QString &error);
    void setReady(bool ready);
    void watch(const QString &modelPath);

    EngineContext m_ctx;
    QThread *m_thread = nullptr;
    WhisperWorker *m_worker = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_rescan = nullptr;
    QString m_pathOverride;
    QString m_requestedPath; // last model sent to the worker
    QString m_loadError;
    FileStamp m_loadedStamp;
    quint64 m_loadSerial = 0;
    bool m_loading = false;
    bool m_ready = false;
};
