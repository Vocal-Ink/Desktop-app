#include "models/ModelManager.h"

#include "core/NetworkUtil.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

const QString kWhisperBase = QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main");
const QString kPiperVoicesBase = QStringLiteral("https://huggingface.co/rhasspy/piper-voices/resolve/main");
const QString kPiperRuntimeBase = QStringLiteral("https://github.com/rhasspy/piper/releases/download/2023.11.14-2/");
const QString kWhisperTask = QStringLiteral("whisper:");
const QString kVoiceTask = QStringLiteral("piper-voice:");
const QString kRuntimeTask = QStringLiteral("piper-runtime");

constexpr int kTransferTimeoutMs = 60000; // without any data
constexpr int kExtractTimeoutMs = 180000;
constexpr int kProgressIntervalMs = 100;

// ggml model files start with the magic 0x67676d6c ("ggml") stored little-endian.
const QByteArray kGgmlMagic = QByteArrayLiteral("lmgg");

bool isSafeWhisperFile(const QString &file)
{
    static const QRegularExpression re(QStringLiteral("^ggml-[A-Za-z0-9._-]+\\.bin$"));
    return re.match(file).hasMatch();
}

bool isSafeVoiceKey(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-][A-Za-z0-9._-]*$"));
    return re.match(key).hasMatch();
}

QString withoutTrailingSlash(QString url)
{
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

int qualityRank(const QString &quality)
{
    static const QStringList order{QStringLiteral("x_low"), QStringLiteral("low"), QStringLiteral("medium"),
                                   QStringLiteral("high")};
    const int i = int(order.indexOf(quality));
    return i < 0 ? int(order.size()) : i;
}

QString piperVoicesCachePath()
{
    return Paths::dataDir() + QStringLiteral("/piper-voices.json");
}

QString tarProgram()
{
#ifdef Q_OS_WIN
    // The bsdtar that ships with Windows 10+ (reads zip too), not a GNU tar from Git/MSYS.
    const QString system = qEnvironmentVariable("SystemRoot") + QStringLiteral("\\System32\\tar.exe");
    if (QFileInfo::exists(system))
        return system;
#endif
    const QString found = QStandardPaths::findExecutable(QStringLiteral("tar"));
    return found.isEmpty() ? QStringLiteral("tar") : found;
}

QList<PiperVoiceInfo> parsePiperVoices(const QByteArray &json)
{
    QList<PiperVoiceInfo> voices;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return voices;
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        PiperVoiceInfo v;
        v.key = o.value(QLatin1String("key")).toString(it.key());
        if (!isSafeVoiceKey(v.key))
            continue;
        v.name = o.value(QLatin1String("name")).toString();
        v.quality = o.value(QLatin1String("quality")).toString();
        v.numSpeakers = std::max(1, o.value(QLatin1String("num_speakers")).toInt(1));
        const QJsonObject lang = o.value(QLatin1String("language")).toObject();
        v.languageCode = lang.value(QLatin1String("code")).toString();
        const QString english = lang.value(QLatin1String("name_english")).toString();
        const QString country = lang.value(QLatin1String("country_english")).toString();
        v.languageName = country.isEmpty() ? english : QStringLiteral("%1 (%2)").arg(english, country);
        if (v.languageName.isEmpty())
            v.languageName = lang.value(QLatin1String("name_native")).toString(v.languageCode);

        bool hasModel = false;
        bool hasConfig = false;
        const QJsonObject files = o.value(QLatin1String("files")).toObject();
        for (auto f = files.begin(); f != files.end(); ++f) {
            const bool config = f.key().endsWith(QLatin1String(".onnx.json"));
            const bool model = !config && f.key().endsWith(QLatin1String(".onnx"));
            if (!config && !model)
                continue;
            const QJsonObject meta = f.value().toObject();
            PiperVoiceInfo::File file;
            file.relPath = f.key();
            file.size = qint64(meta.value(QLatin1String("size_bytes")).toDouble());
            file.md5 = meta.value(QLatin1String("md5_digest")).toString().toLower();
            v.files << file;
            hasModel = hasModel || model;
            hasConfig = hasConfig || config;
        }
        if (!hasModel || !hasConfig)
            continue;
        // Model first, then its config.
        std::stable_sort(v.files.begin(), v.files.end(), [](const PiperVoiceInfo::File &a, const PiperVoiceInfo::File &b) {
            return !a.relPath.endsWith(QLatin1String(".json")) && b.relPath.endsWith(QLatin1String(".json"));
        });
        voices << v;
    }
    std::sort(voices.begin(), voices.end(), [](const PiperVoiceInfo &a, const PiperVoiceInfo &b) {
        const int byLanguage = QString::compare(a.languageName, b.languageName, Qt::CaseInsensitive);
        if (byLanguage != 0)
            return byLanguage < 0;
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return qualityRank(a.quality) < qualityRank(b.quality);
    });
    return voices;
}

} // namespace

qint64 PiperVoiceInfo::totalBytes() const
{
    qint64 t = 0;
    for (const File &f : files)
        t += f.size;
    return t;
}

// --- Download tasks ----------------------------------------------------------------

class ModelManager::Private
{
public:
    struct Item
    {
        QUrl url;
        QString dest;
        QString md5;         // expected hex digest; empty = not checked
        qint64 size = 0;     // expected or approximate size, for progress
        bool ggmlMagic = false;
    };

    struct Task
    {
        QString id;
        QList<Item> items;
        int current = -1;
        std::vector<std::unique_ptr<QSaveFile>> files; // committed together once all are verified
        QPointer<QNetworkReply> reply;
        QCryptographicHash md5{QCryptographicHash::Md5};
        QByteArray head;       // first bytes of the current item
        QByteArray errorBody;
        qint64 itemBytes = 0;  // written for the current item
        qint64 doneBytes = 0;  // completed items
        QElapsedTimer progressClock;
        QString archive;       // runtime: archive to unpack after the download
        QString staging;       // runtime: unpack location
        QString cleanupDir;    // removed if it ends up empty
        QPointer<QProcess> process;
    };

    explicit Private(ModelManager *q)
        : q(q)
    {
    }

    ~Private()
    {
        const QList<Task *> all = tasks.values();
        tasks.clear();
        for (Task *task : all) {
            discard(task);
            delete task;
        }
        if (catalogReply) {
            catalogReply->disconnect(q);
            catalogReply->abort();
            catalogReply->deleteLater();
        }
    }

    void start(Task *task);
    void startNextItem(Task *task);
    bool consume(Task *task);
    void onProgress(Task *task, qint64 received, qint64 total);
    void onItemFinished(Task *task);
    void commitAll(Task *task);
    void extract(Task *task);
    void installRuntime(Task *task);
    void succeed(Task *task);
    void fail(Task *task, const QString &error);
    void discard(Task *task);
    bool isAlive(const QString &id, const Task *task) const { return tasks.value(id) == task; }

    bool loadCatalogCache();
    void onCatalogFinished(QNetworkReply *reply);

    ModelManager *q;
    QHash<QString, Task *> tasks;
    QPointer<QNetworkReply> catalogReply;
};

void ModelManager::Private::start(Task *task)
{
    tasks.insert(task->id, task);
    if (!q->m_network) {
        fail(task, ModelManager::tr("Downloads are not available (no network access)."));
        return;
    }
    task->progressClock.start();
    qint64 total = 0;
    for (const Item &item : std::as_const(task->items))
        total += item.size;
    emit q->downloadProgress(task->id, 0, total);
    startNextItem(task);
}

void ModelManager::Private::startNextItem(Task *task)
{
    ++task->current;
    if (task->current >= task->items.size()) {
        commitAll(task);
        return;
    }
    const Item &item = task->items.at(task->current);
    QDir().mkpath(QFileInfo(item.dest).absolutePath());
    auto file = std::make_unique<QSaveFile>(item.dest);
    if (!file->open(QIODevice::WriteOnly)) {
        fail(task, ModelManager::tr("Could not write %1: %2").arg(QDir::toNativeSeparators(item.dest), file->errorString()));
        return;
    }
    task->files.push_back(std::move(file));
    task->md5.reset();
    task->head.clear();
    task->errorBody.clear();
    task->itemBytes = 0;

    QNetworkReply *reply = q->m_network->get(NetworkUtil::jsonRequest(item.url, kTransferTimeoutMs));
    task->reply = reply;
    QObject::connect(reply, &QNetworkReply::readyRead, q, [this, task] { consume(task); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, q, [this, task](qint64 received, qint64 total) {
        onProgress(task, received, total);
    });
    QObject::connect(reply, &QNetworkReply::finished, q, [this, task] { onItemFinished(task); });
}

// Moves what has arrived into the file. Returns false if the task failed (and is gone).
bool ModelManager::Private::consume(Task *task)
{
    QNetworkReply *reply = task->reply;
    if (!reply)
        return true;
    const QByteArray data = reply->readAll();
    if (data.isEmpty())
        return true;
    if (NetworkUtil::httpStatus(reply) >= 300) {
        task->errorBody += data.left(qMax<qsizetype>(0, 4096 - task->errorBody.size()));
        return true;
    }
    const Item &item = task->items.at(task->current);
    if (task->head.size() < kGgmlMagic.size()) {
        task->head += data.left(kGgmlMagic.size() - task->head.size());
        if (item.ggmlMagic && task->head.size() == kGgmlMagic.size() && task->head != kGgmlMagic) {
            fail(task, ModelManager::tr("The downloaded file is not a Whisper model."));
            return false;
        }
    }
    task->md5.addData(data);
    QSaveFile *file = task->files.back().get();
    if (file->write(data) != data.size()) {
        fail(task, ModelManager::tr("Could not save the download: %1").arg(file->errorString()));
        return false;
    }
    task->itemBytes += data.size();
    return true;
}

void ModelManager::Private::onProgress(Task *task, qint64 received, qint64 total)
{
    if (!task->reply || NetworkUtil::httpStatus(task->reply) >= 300)
        return;
    const bool last = total > 0 && received >= total;
    if (!last && task->progressClock.elapsed() < kProgressIntervalMs)
        return;
    task->progressClock.restart();

    const Item &item = task->items.at(task->current);
    qint64 overall = task->doneBytes + (total > 0 ? total : item.size);
    for (int i = task->current + 1; i < task->items.size(); ++i)
        overall += task->items.at(i).size;
    emit q->downloadProgress(task->id, task->doneBytes + received, overall);
}

void ModelManager::Private::onItemFinished(Task *task)
{
    if (!consume(task))
        return;
    QNetworkReply *reply = task->reply;
    task->reply = nullptr;
    reply->deleteLater();
    const Item &item = task->items.at(task->current);

    if (reply->error() != QNetworkReply::NoError || NetworkUtil::httpStatus(reply) >= 300) {
        fail(task, NetworkUtil::describeError(reply, task->errorBody, item.url.host(), NetworkUtil::KeyPlace::NoKey));
        return;
    }
    if (item.ggmlMagic && task->head != kGgmlMagic) {
        fail(task, ModelManager::tr("The downloaded file is not a Whisper model."));
        return;
    }
    if (!item.md5.isEmpty() && QString::fromLatin1(task->md5.result().toHex()) != item.md5) {
        fail(task, ModelManager::tr("The download of %1 was damaged (checksum mismatch). Please try again.")
                       .arg(QFileInfo(item.dest).fileName()));
        return;
    }
    task->doneBytes += task->itemBytes;
    startNextItem(task);
}

void ModelManager::Private::commitAll(Task *task)
{
    QStringList committed;
    for (const std::unique_ptr<QSaveFile> &file : task->files) {
        if (!file->commit()) {
            for (const QString &path : std::as_const(committed))
                QFile::remove(path);
            fail(task, ModelManager::tr("Could not save %1: %2")
                           .arg(QDir::toNativeSeparators(file->fileName()), file->errorString()));
            return;
        }
        committed << file->fileName();
    }
    task->files.clear();
    if (!task->archive.isEmpty())
        extract(task);
    else
        succeed(task);
}

void ModelManager::Private::extract(Task *task)
{
    task->staging = Paths::piperRuntimeDir() + QStringLiteral("/.unpack");
    QDir(task->staging).removeRecursively();
    QDir().mkpath(task->staging);

    const QString id = task->id;
    auto *process = new QProcess(q);
    task->process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    // Relative archive name: a GNU tar would read "C:\..." as a remote host.
    process->setWorkingDirectory(QFileInfo(task->archive).absolutePath());

    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, q, [this, id, task] {
        if (isAlive(id, task))
            fail(task, ModelManager::tr("Unpacking Piper took too long."));
    });
    QObject::connect(process, &QProcess::errorOccurred, q, [this, id, task](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && isAlive(id, task))
            fail(task, ModelManager::tr("Could not unpack Piper: the \"tar\" tool was not found on this computer."));
    });
    QObject::connect(process, &QProcess::finished, q, [this, id, task](int exitCode, QProcess::ExitStatus status) {
        if (!isAlive(id, task))
            return;
        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString output = QString::fromLocal8Bit(task->process ? task->process->readAll() : QByteArray());
            fail(task, ModelManager::tr("Could not unpack Piper (tar exit code %1). %2").arg(exitCode).arg(output.trimmed().left(300)));
            return;
        }
        installRuntime(task);
    });
    timeout->start(kExtractTimeoutMs);
    process->start(tarProgram(), {QStringLiteral("-xf"), QFileInfo(task->archive).fileName(),
                                  QStringLiteral("-C"), QDir::toNativeSeparators(task->staging)});
}

void ModelManager::Private::installRuntime(Task *task)
{
    const QString unpacked = task->staging + QStringLiteral("/piper");
    const QString exe = QFileInfo(ModelManager::bundledPiperExecutable()).fileName();
    if (!QFileInfo(unpacked + QLatin1Char('/') + exe).isFile()) {
        fail(task, ModelManager::tr("The Piper download did not contain %1.").arg(exe));
        return;
    }
    const QString target = Paths::piperRuntimeDir() + QStringLiteral("/piper");
    if (QFileInfo::exists(target) && !QDir(target).removeRecursively()) {
        fail(task, ModelManager::tr("Could not replace the installed Piper. Close anything that is using it and try again."));
        return;
    }
    if (!QDir().rename(unpacked, target)) {
        fail(task, ModelManager::tr("Could not install Piper into %1.").arg(QDir::toNativeSeparators(target)));
        return;
    }
#ifndef Q_OS_WIN
    QFile binary(ModelManager::bundledPiperExecutable());
    binary.setPermissions(binary.permissions() | QFileDevice::ReadOwner | QFileDevice::ExeOwner
                          | QFileDevice::ExeGroup | QFileDevice::ExeOther);
#endif
    succeed(task);
}

void ModelManager::Private::succeed(Task *task)
{
    const QString id = task->id;
    tasks.remove(id);
    discard(task); // temporary archive, unpack folder
    delete task;
    emit q->installedChanged();
    emit q->downloadFinished(id);
}

void ModelManager::Private::fail(Task *task, const QString &error)
{
    const QString id = task->id;
    tasks.remove(id);
    discard(task);
    delete task;
    emit q->downloadFailed(id, error);
}

void ModelManager::Private::discard(Task *task)
{
    if (task->reply) {
        task->reply->disconnect(q);
        task->reply->abort();
        task->reply->deleteLater();
        task->reply = nullptr;
    }
    task->files.clear(); // uncommitted QSaveFiles drop their temporary files
    if (task->process) {
        task->process->disconnect(q);
        if (task->process->state() != QProcess::NotRunning) {
            task->process->kill();
            task->process->waitForFinished(3000);
        }
        task->process->deleteLater();
        task->process = nullptr;
    }
    if (!task->archive.isEmpty())
        QFile::remove(task->archive);
    if (!task->staging.isEmpty())
        QDir(task->staging).removeRecursively();
    if (!task->cleanupDir.isEmpty())
        QDir().rmdir(task->cleanupDir);
}

// --- Piper catalog -------------------------------------------------------------------

bool ModelManager::Private::loadCatalogCache()
{
    QFile file(piperVoicesCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QList<PiperVoiceInfo> voices = parsePiperVoices(file.readAll());
    if (voices.isEmpty())
        return false;
    q->m_piperCatalog = voices;
    return true;
}

void ModelManager::Private::onCatalogFinished(QNetworkReply *reply)
{
    catalogReply = nullptr;
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    const bool haveList = !q->m_piperCatalog.isEmpty();
    if (reply->error() != QNetworkReply::NoError || NetworkUtil::httpStatus(reply) >= 300) {
        const QString error = NetworkUtil::describeError(reply, body, reply->url().host(), NetworkUtil::KeyPlace::NoKey);
        emit q->piperCatalogError(haveList ? ModelManager::tr("Could not update the Piper voice list (%1). Showing the saved list.").arg(error)
                                           : ModelManager::tr("Could not download the Piper voice list: %1").arg(error));
        return;
    }
    const QList<PiperVoiceInfo> voices = parsePiperVoices(body);
    if (voices.isEmpty()) {
        emit q->piperCatalogError(ModelManager::tr("The Piper voice list could not be read."));
        return;
    }
    QSaveFile cache(piperVoicesCachePath());
    if (cache.open(QIODevice::WriteOnly)) {
        cache.write(body);
        cache.commit();
    }
    q->m_piperCatalog = voices;
    emit q->piperCatalogChanged();
}

// --- ModelManager ----------------------------------------------------------------------

ModelManager::ModelManager(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , d(new Private(this))
    , m_network(network)
{
    d->loadCatalogCache(); // the list works offline
}

ModelManager::~ModelManager()
{
    delete d;
}

QList<WhisperModelInfo> ModelManager::whisperCatalog()
{
    const auto model = [](const char *file, const QString &title, const QString &description, qint64 bytes,
                          bool multilingual) {
        WhisperModelInfo m;
        m.file = QString::fromLatin1(file);
        m.title = title;
        m.description = description;
        m.approxBytes = bytes;
        m.multilingual = multilingual;
        return m;
    };
    return {
        model("ggml-tiny.en-q5_1.bin", tr("Tiny (English)"),
              tr("Fastest and smallest; less accurate. Good for older computers."), 32'200'000, false),
        model("ggml-base.en-q5_1.bin", tr("Base (English)"),
              tr("Fast and accurate for English."), 59'700'000, false),
        model("ggml-small.en-q5_1.bin", tr("Small (English)"),
              tr("Most accurate English model; slower."), 190'000'000, false),
        model("ggml-base-q5_1.bin", tr("Base (multilingual)"),
              tr("Fast, understands many languages."), 59'700'000, true),
        model("ggml-small-q5_1.bin", tr("Small (multilingual)"),
              tr("More accurate in many languages; slower."), 190'000'000, true),
        model("ggml-large-v3-turbo-q5_0.bin", tr("Large v3 Turbo (multilingual)"),
              tr("Best accuracy in every language; needs a fast computer."), 574'000'000, true),
    };
}

QStringList ModelManager::installedWhisperModels() const
{
    return QDir(Paths::whisperModelsDir()).entryList({QStringLiteral("ggml-*.bin")}, QDir::Files, QDir::Name);
}

bool ModelManager::isWhisperModelInstalled(const QString &file) const
{
    return isSafeWhisperFile(file) && QFileInfo(whisperModelPath(file)).isFile();
}

QString ModelManager::whisperModelPath(const QString &file)
{
    return Paths::whisperModelsDir() + QLatin1Char('/') + file;
}

void ModelManager::downloadWhisperModel(const QString &file)
{
    const QString id = kWhisperTask + file;
    if (isDownloading(id))
        return;
    if (!isSafeWhisperFile(file)) {
        emit downloadFailed(id, tr("\"%1\" is not a Whisper model file name.").arg(file));
        return;
    }
    Private::Item item;
    item.url = QUrl(withoutTrailingSlash(m_whisperBase.isEmpty() ? kWhisperBase : m_whisperBase) + QLatin1Char('/') + file);
    item.dest = whisperModelPath(file);
    item.ggmlMagic = true;
    const QList<WhisperModelInfo> catalog = whisperCatalog();
    for (const WhisperModelInfo &info : catalog) {
        if (info.file == file)
            item.size = info.approxBytes;
    }
    auto *task = new Private::Task;
    task->id = id;
    task->items << item;
    d->start(task);
}

bool ModelManager::removeWhisperModel(const QString &file)
{
    if (!isSafeWhisperFile(file))
        return false;
    cancel(kWhisperTask + file);
    const QString path = whisperModelPath(file);
    if (!QFileInfo::exists(path) || !QFile::remove(path))
        return false;
    emit installedChanged();
    return true;
}

QUrl ModelManager::piperRuntimeUrl()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    const bool x64 = arch == QLatin1String("x86_64");
    const bool arm64 = arch == QLatin1String("arm64");
    QString file;
#if defined(Q_OS_WIN)
    if (x64 || arm64) // Windows on ARM runs the x64 build
        file = QStringLiteral("piper_windows_amd64.zip");
#elif defined(Q_OS_MACOS)
    if (x64)
        file = QStringLiteral("piper_macos_x64.tar.gz");
    else if (arm64)
        file = QStringLiteral("piper_macos_aarch64.tar.gz");
#elif defined(Q_OS_LINUX)
    if (x64)
        file = QStringLiteral("piper_linux_x86_64.tar.gz");
    else if (arm64)
        file = QStringLiteral("piper_linux_aarch64.tar.gz");
#endif
    Q_UNUSED(x64)
    Q_UNUSED(arm64)
    return file.isEmpty() ? QUrl() : QUrl(kPiperRuntimeBase + file);
}

QString ModelManager::bundledPiperExecutable()
{
#ifdef Q_OS_WIN
    return Paths::piperRuntimeDir() + QStringLiteral("/piper/piper.exe");
#else
    return Paths::piperRuntimeDir() + QStringLiteral("/piper/piper");
#endif
}

bool ModelManager::isPiperRuntimeInstalled() const
{
    return QFileInfo(bundledPiperExecutable()).isFile();
}

void ModelManager::downloadPiperRuntime()
{
    if (isDownloading(kRuntimeTask))
        return;
    const QUrl url = m_runtimeOverride.isEmpty() ? piperRuntimeUrl() : m_runtimeOverride;
    if (url.isEmpty()) {
        emit downloadFailed(kRuntimeTask, tr("Piper is not available for this kind of computer."));
        return;
    }
    QString name = url.fileName();
    if (name.isEmpty())
        name = QStringLiteral("piper-runtime.tar.gz");
    Private::Item item;
    item.url = url;
    item.dest = Paths::ensureDir(Paths::dataDir() + QStringLiteral("/downloads")) + QLatin1Char('/') + name;
    item.size = 25'000'000;
    auto *task = new Private::Task;
    task->id = kRuntimeTask;
    task->items << item;
    task->archive = item.dest;
    d->start(task);
}

void ModelManager::refreshPiperCatalog()
{
    if (d->catalogReply)
        return;
    if (m_piperCatalog.isEmpty() && d->loadCatalogCache())
        emit piperCatalogChanged();
    if (!m_network) {
        emit piperCatalogError(tr("Could not download the Piper voice list (no network access)."));
        return;
    }
    const QString base = withoutTrailingSlash(m_piperVoicesBase.isEmpty() ? kPiperVoicesBase : m_piperVoicesBase);
    QNetworkReply *reply = m_network->get(NetworkUtil::jsonRequest(QUrl(base + QStringLiteral("/voices.json")), kTransferTimeoutMs));
    d->catalogReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { d->onCatalogFinished(reply); });
}

bool ModelManager::isPiperVoiceInstalled(const QString &key) const
{
    if (!isSafeVoiceKey(key))
        return false;
    const QString model = piperVoiceModelPath(key);
    return QFileInfo(model).isFile() && QFileInfo(model + QStringLiteral(".json")).isFile();
}

QString ModelManager::piperVoiceModelPath(const QString &key)
{
    return Paths::piperVoicesDir() + QLatin1Char('/') + key + QLatin1Char('/') + key + QStringLiteral(".onnx");
}

void ModelManager::downloadPiperVoice(const QString &key)
{
    const QString id = kVoiceTask + key;
    if (isDownloading(id))
        return;
    const auto found = std::find_if(m_piperCatalog.cbegin(), m_piperCatalog.cend(),
                                    [&key](const PiperVoiceInfo &v) { return v.key == key; });
    if (!isSafeVoiceKey(key) || found == m_piperCatalog.cend()) {
        emit downloadFailed(id, tr("The voice \"%1\" is not in the Piper voice list. Refresh the list and try again.").arg(key));
        return;
    }
    const QString base = withoutTrailingSlash(m_piperVoicesBase.isEmpty() ? kPiperVoicesBase : m_piperVoicesBase);
    const QString model = piperVoiceModelPath(key);
    auto *task = new Private::Task;
    task->id = id;
    task->cleanupDir = QFileInfo(model).absolutePath();
    for (const PiperVoiceInfo::File &file : found->files) {
        Private::Item item;
        item.url = QUrl(base + QLatin1Char('/') + file.relPath);
        item.dest = file.relPath.endsWith(QLatin1String(".json")) ? model + QStringLiteral(".json") : model;
        item.md5 = file.md5;
        item.size = file.size;
        task->items << item;
    }
    d->start(task);
}

bool ModelManager::removePiperVoice(const QString &key)
{
    if (!isSafeVoiceKey(key))
        return false;
    cancel(kVoiceTask + key);
    QDir dir(Paths::piperVoicesDir() + QLatin1Char('/') + key);
    if (!dir.exists())
        return false;
    const bool removed = dir.removeRecursively();
    emit installedChanged();
    return removed;
}

QString ModelManager::recommendedPiperVoice()
{
    return QStringLiteral("en_US-lessac-medium");
}

QString ModelManager::recommendedPiperVoice(const QList<PiperVoiceInfo> &catalog, const QString &locale)
{
    // Clear, natural voices picked by ear, per language (first one wins
    // when only the language matches).
    static const QList<std::pair<QString, QString>> picks{
        {QStringLiteral("en_US"), QStringLiteral("en_US-lessac-medium")},
        {QStringLiteral("en_GB"), QStringLiteral("en_GB-alba-medium")},
        {QStringLiteral("es_ES"), QStringLiteral("es_ES-davefx-medium")},
        {QStringLiteral("fr_FR"), QStringLiteral("fr_FR-siwis-medium")},
        {QStringLiteral("de_DE"), QStringLiteral("de_DE-thorsten-medium")},
        {QStringLiteral("pt_BR"), QStringLiteral("pt_BR-faber-medium")},
        {QStringLiteral("it_IT"), QStringLiteral("it_IT-paola-medium")},
        {QStringLiteral("pl_PL"), QStringLiteral("pl_PL-darkman-medium")},
        {QStringLiteral("tr_TR"), QStringLiteral("tr_TR-dfki-medium")},
        {QStringLiteral("zh_CN"), QStringLiteral("zh_CN-huayan-medium")},
    };
    const QString language = locale.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).toLower();
    const QString exact = QString(locale).replace(QLatin1Char('-'), QLatin1Char('_'));
    const auto has = [&catalog](const QString &key) {
        return std::any_of(catalog.cbegin(), catalog.cend(), [&key](const PiperVoiceInfo &v) { return v.key == key; });
    };
    if (language.isEmpty() || language == QLatin1String("c"))
        return recommendedPiperVoice();

    // The exact region first ("pt_BR" is not "pt_PT"), then any region.
    for (const bool sameRegion : {true, false}) {
        const auto matches = [&](const PiperVoiceInfo &v) {
            return sameRegion ? v.languageCode == exact
                              : v.languageCode.section(QLatin1Char('_'), 0, 0) == language;
        };
        for (const auto &[region, key] : picks) {
            const bool fits = sameRegion ? region == exact : region.section(QLatin1Char('_'), 0, 0) == language;
            if (fits && has(key))
                return key;
        }
        const auto rank = [](const PiperVoiceInfo &v) {
            static const QStringList order{QStringLiteral("medium"), QStringLiteral("high"), QStringLiteral("low"),
                                           QStringLiteral("x_low")};
            const qsizetype q = order.indexOf(v.quality);
            return int(q < 0 ? order.size() : q) * 2 + (v.numSpeakers > 1 ? 1 : 0);
        };
        const PiperVoiceInfo *best = nullptr;
        for (const PiperVoiceInfo &v : catalog) {
            if (matches(v) && (!best || rank(v) < rank(*best) || (rank(v) == rank(*best) && v.key < best->key)))
                best = &v;
        }
        if (best)
            return best->key;
    }
    return recommendedPiperVoice();
}

QString ModelManager::recommendedWhisperModel(const QString &language)
{
    const QString code = language.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).toLower();
    const bool english = code.isEmpty() || code == QLatin1String("en") || code == QLatin1String("c");
    return english ? QStringLiteral("ggml-base.en-q5_1.bin") : QStringLiteral("ggml-base-q5_1.bin");
}

bool ModelManager::isDownloading(const QString &taskId) const
{
    return d->tasks.contains(taskId);
}

void ModelManager::cancel(const QString &taskId)
{
    if (Private::Task *task = d->tasks.value(taskId))
        d->fail(task, tr("Download cancelled."));
}
