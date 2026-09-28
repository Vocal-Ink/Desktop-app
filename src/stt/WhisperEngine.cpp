#include "stt/WhisperEngine.h"

#include "core/Paths.h"
#include "core/Settings.h"
#include "core/TextProcessor.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <thread>

#ifdef VOCALINK_HAVE_WHISPER
#include <whisper.h>
#endif

namespace {

constexpr int kSampleRate = 16000;
constexpr qsizetype kMinSamples = kSampleRate * 11 / 10; // whisper.cpp rejects input under 1 s

#ifdef VOCALINK_HAVE_WHISPER
void whisperLog(enum ggml_log_level level, const char *text, void *)
{
    if (level == GGML_LOG_LEVEL_ERROR && text)
        qWarning("whisper: %s", QByteArray(text).trimmed().constData());
}

void installWhisperLogger()
{
    static const bool installed = [] {
        whisper_log_set(whisperLog, nullptr);
        return true;
    }();
    Q_UNUSED(installed)
}

int threadCount()
{
    return std::clamp(int(std::thread::hardware_concurrency()), 2, 8);
}

QByteArray nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return path.toUtf8(); // whisper.cpp widens UTF-8 paths itself on Windows
#else
    return QFile::encodeName(path);
#endif
}

// "en", "en-US", "EN_us" -> "en"; empty for auto-detect.
QByteArray languageCode(const QString &language)
{
    QString code = language.trimmed().toLower();
    const qsizetype sep = code.indexOf(QRegularExpression(QStringLiteral("[-_]")));
    if (sep > 0)
        code.truncate(sep);
    if (code.isEmpty() || code == QLatin1String("auto"))
        return {};
    return code.toLatin1();
}
#endif

} // namespace

// Lives on the engine's worker thread and owns the whisper context.
class WhisperWorker : public QObject
{
    Q_OBJECT
public:
    ~WhisperWorker() override { unload(); }

    void load(quint64 serial, const QString &path);
    void transcribe(quint64 requestId, QVector<float> audio, const QString &language, const QString &prompt);

    std::atomic<bool> abort{false};

signals:
    void loaded(quint64 serial, const QString &path, bool ok, const QString &error);
    void transcribed(quint64 requestId, const QString &text);
    void failed(quint64 requestId, const QString &error);

private:
    void unload();

#ifdef VOCALINK_HAVE_WHISPER
    whisper_context *m_ctx = nullptr;
#endif
};

void WhisperWorker::unload()
{
#ifdef VOCALINK_HAVE_WHISPER
    if (m_ctx) {
        whisper_free(m_ctx);
        m_ctx = nullptr;
    }
#endif
}

void WhisperWorker::load(quint64 serial, const QString &path)
{
    unload();
    if (path.isEmpty() || abort)
        return;
#ifdef VOCALINK_HAVE_WHISPER
    whisper_context_params params = whisper_context_default_params();
    m_ctx = whisper_init_from_file_with_params(nativePath(path).constData(), params);
    if (m_ctx)
        emit loaded(serial, path, true, QString());
    else
        emit loaded(serial, path, false, tr("\"%1\" is not a valid Whisper model.").arg(QFileInfo(path).fileName()));
#else
    emit loaded(serial, path, false, tr("Local speech recognition is not included in this build."));
#endif
}

void WhisperWorker::transcribe(quint64 requestId, QVector<float> audio, const QString &language, const QString &prompt)
{
    if (abort)
        return;
#ifdef VOCALINK_HAVE_WHISPER
    if (!m_ctx) {
        emit failed(requestId, tr("The speech model is not loaded."));
        return;
    }
    if (audio.size() < kMinSamples) {
        const qsizetype old = audio.size();
        audio.resize(kMinSamples);
        std::fill(audio.begin() + old, audio.end(), 0.0f);
    }

    QByteArray lang = languageCode(language);
    if (!whisper_is_multilingual(m_ctx))
        lang = QByteArrayLiteral("en");
    else if (lang.isEmpty() || whisper_lang_id(lang.constData()) < 0)
        lang = QByteArrayLiteral("auto");
    const QByteArray promptUtf8 = prompt.trimmed().toUtf8();

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = threadCount();
    params.language = lang.constData();
    params.detect_language = false;
    params.translate = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = false;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.suppress_blank = true;
    params.suppress_nst = true;
    params.initial_prompt = promptUtf8.isEmpty() ? nullptr : promptUtf8.constData();
    params.abort_callback = [](void *data) -> bool { return static_cast<std::atomic<bool> *>(data)->load(); };
    params.abort_callback_user_data = &abort;

    const int rc = whisper_full(m_ctx, params, audio.constData(), int(audio.size()));
    if (abort)
        return;
    if (rc != 0) {
        emit failed(requestId, tr("Speech recognition failed (whisper error %1).").arg(rc));
        return;
    }
    QByteArray text;
    const int segments = whisper_full_n_segments(m_ctx);
    for (int i = 0; i < segments; ++i)
        text += whisper_full_get_segment_text(m_ctx, i);
    emit transcribed(requestId, TextProcessor::cleanTranscript(QString::fromUtf8(text)));
#else
    Q_UNUSED(audio)
    Q_UNUSED(language)
    Q_UNUSED(prompt)
    emit failed(requestId, tr("Local speech recognition is not included in this build."));
#endif
}

// --- WhisperEngine ---------------------------------------------------------------

WhisperEngine::WhisperEngine(const EngineContext &context, QObject *parent)
    : SttEngine(parent)
    , m_ctx(context)
    , m_thread(new QThread(this))
    , m_worker(new WhisperWorker)
    , m_watcher(new QFileSystemWatcher(this))
    , m_rescan(new QTimer(this))
{
#ifdef VOCALINK_HAVE_WHISPER
    installWhisperLogger();
#endif
    m_thread->setObjectName(QStringLiteral("whisper"));
    m_worker->moveToThread(m_thread);
    connect(m_worker, &WhisperWorker::loaded, this, [this](quint64 serial, const QString &path, bool ok, const QString &error) {
        if (serial == m_loadSerial)
            onLoaded(path, ok, error);
    });
    connect(m_worker, &WhisperWorker::transcribed, this, &SttEngine::transcribed);
    connect(m_worker, &WhisperWorker::failed, this, &SttEngine::failed);
    m_thread->start();

    // Pick up a model that is downloaded or copied in later.
    m_rescan->setSingleShot(true);
    m_rescan->setInterval(400);
    connect(m_rescan, &QTimer::timeout, this, &WhisperEngine::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_rescan, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::fileChanged, m_rescan, qOverload<>(&QTimer::start));

    if (m_ctx.settings) {
        connect(m_ctx.settings, &Settings::changed, this, [this](const QString &key) {
            if (key == QLatin1String(Keys::WhisperModel))
                reload();
        });
    }
    reload();
}

WhisperEngine::~WhisperEngine()
{
    m_worker->abort = true;
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
}

QString WhisperEngine::modelPath() const
{
    if (!m_pathOverride.isEmpty())
        return m_pathOverride;
    const QString file = m_ctx.settings ? m_ctx.settings->string(Keys::WhisperModel)
                                        : Settings::defaultValue(Keys::WhisperModel).toString();
    if (file.isEmpty())
        return {};
    if (QFileInfo(file).isAbsolute())
        return file;
    return Paths::whisperModelsDir() + QLatin1Char('/') + file;
}

void WhisperEngine::setModelPathForTesting(const QString &path)
{
    m_pathOverride = path;
    reload();
}

bool WhisperEngine::isReady() const
{
    return m_ready;
}

QString WhisperEngine::notReadyReason() const
{
#ifndef VOCALINK_HAVE_WHISPER
    return tr("Local speech recognition is not included in this build.");
#else
    if (m_ready)
        return {};
    if (m_loading)
        return tr("Loading speech model…");
    if (!m_loadError.isEmpty())
        return tr("%1 Download it again in Settings → Speech input.").arg(m_loadError);
    return tr("Download a speech recognition model in Settings → Speech input");
#endif
}

WhisperEngine::FileStamp WhisperEngine::stampOf(const QString &path)
{
    const QFileInfo info(path);
    FileStamp s;
    if (info.exists()) {
        s.size = info.size();
        s.modified = info.lastModified();
    }
    return s;
}

void WhisperEngine::watch(const QString &modelPath)
{
    QStringList wanted{QFileInfo(modelPath).absolutePath()};
    if (QFileInfo::exists(modelPath))
        wanted << modelPath;
    QStringList current = m_watcher->directories() + m_watcher->files();
    std::sort(wanted.begin(), wanted.end());
    std::sort(current.begin(), current.end());
    if (wanted == current)
        return;
    if (!current.isEmpty())
        m_watcher->removePaths(current);
    m_watcher->addPaths(wanted);
}

void WhisperEngine::reload()
{
    const QString path = modelPath();
    if (!path.isEmpty())
        watch(path);

    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        if (!m_requestedPath.isEmpty()) {
            // The model went away: free its memory.
            const quint64 serial = ++m_loadSerial;
            QMetaObject::invokeMethod(m_worker, [w = m_worker, serial] { w->load(serial, QString()); }, Qt::QueuedConnection);
        }
        m_requestedPath.clear();
        m_loadError.clear();
        m_loadedStamp = FileStamp();
        m_loading = false;
        setReady(false);
        return;
    }

    const FileStamp stamp = stampOf(path);
    if (path == m_requestedPath && stamp == m_loadedStamp)
        return; // already loading, loaded, or known to be broken

    m_requestedPath = path;
    m_loadedStamp = stamp;
    m_loadError.clear();
    m_loading = true;
    setReady(false);
    const quint64 serial = ++m_loadSerial;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, serial, path] { w->load(serial, path); }, Qt::QueuedConnection);
}

void WhisperEngine::onLoaded(const QString &path, bool ok, const QString &error)
{
    m_loading = false;
    if (ok) {
        m_loadError.clear();
        setReady(true);
        emit modelLoaded(path);
    } else {
        m_loadError = error;
        setReady(false);
        emit modelLoadFailed(path, error);
    }
}

void WhisperEngine::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged(ready);
}

void WhisperEngine::transcribe(quint64 requestId, const QVector<float> &mono16k)
{
    if (!m_ready) {
        const QString reason = notReadyReason();
        QMetaObject::invokeMethod(this, [this, requestId, reason] { emit failed(requestId, reason); }, Qt::QueuedConnection);
        return;
    }
    const QString language = m_options.language;
    const QString prompt = m_options.prompt;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, requestId, mono16k, language, prompt] {
        w->transcribe(requestId, mono16k, language, prompt);
    }, Qt::QueuedConnection);
}

#include "WhisperEngine.moc"
