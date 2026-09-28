#include "tts/SystemTtsEngine.h"

#include "tts/StreamHelpers.h"

#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#include <QAudioFormat>
#include <QLocale>
#include <QPointer>
#include <QSet>
#include <QTextToSpeech>
#include <QTimer>
#include <QVoice>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace {

// Longest silence from a synthesizing engine before the request is given up.
constexpr int kStallTimeoutMs = 30000;

// Handed out for one request; cancelling it tells the engine.
class SystemTtsStream : public TtsStream
{
public:
    explicit SystemTtsStream(std::function<void()> onCancelled)
        : m_onCancelled(std::move(onCancelled))
    {
    }
    using TtsStream::deliverAudio;
    using TtsStream::deliverFailed;
    using TtsStream::deliverFailedLater;
    using TtsStream::deliverFinished;

protected:
    void onCancel() override
    {
        if (m_onCancelled)
            m_onCancelled();
    }

private:
    std::function<void()> m_onCancelled;
};

struct Backend
{
    QString name;
    QTextToSpeech *tts = nullptr;
    bool voicesMissing = false; // listed no voices (maybe still starting up)
};

struct Job
{
    quint64 id = 0;
    QPointer<SystemTtsStream> stream;
    QString text;
    QString engine;
    QString voiceName;
    QString localeName;
    double rate = 0.0;  // QTextToSpeech scale, -1..1
    double pitch = 0.0; // -1..1
};

} // namespace

// A QTextToSpeech synthesizes one text at a time, so requests are queued and
// run one after another.
struct SystemTtsEngine::Private
{
    explicit Private(SystemTtsEngine *owner)
        : q(owner)
    {
    }

    void ensureBackends();
    Backend *backend(const QString &name);
    QList<Voice> collectVoices();
    void scheduleNext();
    void processQueue();
    void onData(quint64 jobId, const QAudioFormat &format, const QByteArray &bytes);
    void onStateChanged(const QString &engine, QTextToSpeech::State state);
    void onError(const QString &engine, const QString &message);
    void finishActive(const QString &error);
    void cancel(quint64 jobId);

    SystemTtsEngine *q;
    QStringList candidates; // engine plugins worth trying
    bool backendsCreated = false;
    QList<Backend> backends;
    QList<Job> queue;
    std::optional<Job> active;
    bool started = false;   // the active job reached Synthesizing
    QAudioFormat format;    // of the bytes in `pending`
    QByteArray pending;     // incomplete frame
    qint64 delivered = 0;
    QTimer *watchdog = nullptr;
    quint64 nextJobId = 1;
};

void SystemTtsEngine::Private::ensureBackends()
{
    if (backendsCreated)
        return;
    backendsCreated = true;
    for (const QString &name : std::as_const(candidates)) {
        auto *tts = new QTextToSpeech(name, q);
        if (!tts->engineCapabilities().testFlag(QTextToSpeech::Capability::Synthesize)
            || tts->state() == QTextToSpeech::Error) {
            delete tts;
            continue;
        }
        QObject::connect(tts, &QTextToSpeech::stateChanged, q, [this, name](QTextToSpeech::State state) {
            onStateChanged(name, state);
        });
        QObject::connect(tts, &QTextToSpeech::errorOccurred, q,
                         [this, name](QTextToSpeech::ErrorReason, const QString &message) { onError(name, message); });
        Backend b;
        b.name = name;
        b.tts = tts;
        backends.append(b);
    }
}

Backend *SystemTtsEngine::Private::backend(const QString &name)
{
    for (Backend &b : backends) {
        if (b.name == name)
            return &b;
    }
    return nullptr;
}

QList<Voice> SystemTtsEngine::Private::collectVoices()
{
    QList<Voice> list;
    QSet<QString> seen;
    for (Backend &b : backends) {
        const QList<QVoice> voices = b.tts->findVoices(); // every locale
        b.voicesMissing = voices.isEmpty();
        for (const QVoice &qv : voices) {
            const QString localeName = qv.locale().name();
            Voice v;
            v.engineId = q->id();
            v.id = b.name + QLatin1Char('|') + qv.name() + QLatin1Char('|') + localeName;
            if (qv.name().isEmpty() || seen.contains(v.id))
                continue;
            seen.insert(v.id);
            v.name = qv.name();
            v.language = QString(localeName).replace(QLatin1Char('_'), QLatin1Char('-'));
            if (qv.gender() == QVoice::Female)
                v.gender = QStringLiteral("Female");
            else if (qv.gender() == QVoice::Male)
                v.gender = QStringLiteral("Male");
            v.description = b.name;
            list << v;
        }
    }
    std::sort(list.begin(), list.end(), [](const Voice &a, const Voice &b) {
        const int byLanguage = a.language.compare(b.language, Qt::CaseInsensitive);
        if (byLanguage != 0)
            return byLanguage < 0;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return list;
}

void SystemTtsEngine::Private::scheduleNext()
{
    QTimer::singleShot(0, q, [this] { processQueue(); });
}

void SystemTtsEngine::Private::processQueue()
{
    while (!active && !queue.isEmpty()) {
        Job job = queue.takeFirst();
        if (!job.stream || job.stream->isCancelled())
            continue;
        Backend *b = backend(job.engine);
        if (!b) {
            job.stream->deliverFailed(SystemTtsEngine::tr("The system speech engine \"%1\" is not available.").arg(job.engine));
            continue;
        }
        QVoice voice;
        const QList<QVoice> sameLocale = b->tts->findVoices(QLocale(job.localeName));
        for (const QVoice &candidate : sameLocale) {
            if (candidate.name() == job.voiceName) {
                voice = candidate;
                break;
            }
        }
        if (voice.name().isEmpty()) {
            const QList<QVoice> sameName = b->tts->findVoices(job.voiceName);
            if (!sameName.isEmpty())
                voice = sameName.first();
        }
        if (voice.name().isEmpty()) {
            job.stream->deliverFailed(SystemTtsEngine::tr("The system voice \"%1\" is not installed any more.").arg(job.voiceName));
            continue;
        }

        active = job;
        started = false;
        format = QAudioFormat();
        pending.clear();
        delivered = 0;
        b->tts->setVoice(voice);
        b->tts->setRate(job.rate);
        b->tts->setPitch(job.pitch);
        watchdog->start(kStallTimeoutMs);
        const quint64 jobId = job.id;
        b->tts->synthesize(job.text, q, [this, jobId](const QAudioFormat &fmt, const QByteArray &bytes) {
            onData(jobId, fmt, bytes);
        });
        return; // the next job starts once this one reports Ready or Error
    }
}

void SystemTtsEngine::Private::onData(quint64 jobId, const QAudioFormat &fmt, const QByteArray &bytes)
{
    if (!active || active->id != jobId || !active->stream)
        return;
    if (fmt != format) {
        pending.clear();
        format = fmt;
    }
    const int frameBytes = fmt.bytesPerFrame();
    if (frameBytes <= 0 || bytes.isEmpty())
        return;
    pending.append(bytes);
    const qsizetype usable = pending.size() - pending.size() % frameBytes;
    if (usable <= 0)
        return;
    const QByteArray chunk = pending.left(usable);
    pending.remove(0, usable);
    delivered += usable;
    watchdog->start(kStallTimeoutMs);
    active->stream->deliverAudio(fmt, chunk);
}

void SystemTtsEngine::Private::onStateChanged(const QString &engine, QTextToSpeech::State state)
{
    if (!active || active->engine != engine) {
        // An engine that was still starting up may list its voices now.
        Backend *b = backend(engine);
        if (state == QTextToSpeech::Ready && b && b->voicesMissing)
            q->setVoices(collectVoices());
        return;
    }
    switch (state) {
    case QTextToSpeech::Synthesizing:
        started = true;
        break;
    case QTextToSpeech::Ready:
        if (started || delivered > 0)
            finishActive(QString());
        break;
    case QTextToSpeech::Error: {
        const QString message = backend(engine)->tts->errorString();
        finishActive(message.isEmpty() ? SystemTtsEngine::tr("The system voice reported an error.") : message);
        break;
    }
    default:
        break;
    }
}

void SystemTtsEngine::Private::onError(const QString &engine, const QString &message)
{
    if (active && active->engine == engine)
        finishActive(message.isEmpty() ? SystemTtsEngine::tr("The system voice reported an error.") : message);
}

void SystemTtsEngine::Private::finishActive(const QString &error)
{
    if (!active)
        return;
    const Job job = *active;
    active.reset();
    watchdog->stop();
    pending.clear();
    if (!error.isEmpty()) {
        // Resets the engine and releases the synthesize() callback, which Qt
        // keeps after an error. Deferred: we may be inside the engine's signal.
        if (Backend *b = backend(job.engine)) {
            QPointer<QTextToSpeech> tts = b->tts;
            QTimer::singleShot(0, q, [tts] {
                if (tts)
                    tts->stop(QTextToSpeech::BoundaryHint::Immediate);
            });
        }
    }
    if (job.stream) {
        if (!error.isEmpty())
            job.stream->deliverFailed(error);
        else if (delivered == 0)
            job.stream->deliverFailed(SystemTtsEngine::tr("The system voice produced no audio."));
        else
            job.stream->deliverFinished();
    }
    scheduleNext();
}

void SystemTtsEngine::Private::cancel(quint64 jobId)
{
    queue.removeIf([jobId](const Job &j) { return j.id == jobId; });
    if (!active || active->id != jobId)
        return;
    active->stream = nullptr;
    Backend *b = backend(active->engine);
    if (b)
        b->tts->stop(QTextToSpeech::BoundaryHint::Immediate);
    // Engines that stop synchronously have already reported Ready; for the
    // others the job ends when they do (or when the watchdog fires).
    if (active && active->id == jobId && (!b || b->tts->state() != QTextToSpeech::Synthesizing))
        finishActive(QString());
}

SystemTtsEngine::SystemTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
    , d(std::make_unique<Private>(this))
{
    const QStringList engines = QTextToSpeech::availableEngines();
    for (const QString &name : engines) {
        // speechd can only speak aloud, mock is Qt's test engine.
        if (name != QLatin1String("mock") && name != QLatin1String("speechd"))
            d->candidates << name;
    }
    d->watchdog = new QTimer(this);
    d->watchdog->setSingleShot(true);
    connect(d->watchdog, &QTimer::timeout, this, [this] {
        d->finishActive(tr("The system voice stopped responding."));
    });
}

SystemTtsEngine::~SystemTtsEngine()
{
    // Detach the speech engines before the private data goes away.
    for (const Backend &b : std::as_const(d->backends)) {
        b.tts->disconnect(this);
        delete b.tts;
    }
    d->backends.clear();
}

bool SystemTtsEngine::isAvailable() const
{
    return d->backendsCreated ? !d->backends.isEmpty() : !d->candidates.isEmpty();
}

QString SystemTtsEngine::unavailableReason() const
{
    if (isAvailable())
        return {};
    return tr("This system has no speech engine that Vocal Ink can record from. Use Piper or eSpeak NG voices instead.");
}

void SystemTtsEngine::refreshVoices()
{
    const bool wasAvailable = isAvailable();
    d->ensureBackends();
    setVoices(d->collectVoices());
    if (isAvailable() != wasAvailable)
        emit availabilityChanged();
}

TtsStream *SystemTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    const quint64 jobId = d->nextJobId++;
    const QPointer<SystemTtsEngine> self(this);
    auto *stream = new SystemTtsStream([self, jobId] {
        if (self)
            self->d->cancel(jobId);
    });

    // Voice ids are "<engine>|<voice name>|<locale name>".
    const qsizetype first = voice.id.indexOf(QLatin1Char('|'));
    const qsizetype last = voice.id.lastIndexOf(QLatin1Char('|'));
    if (first <= 0 || last <= first) {
        stream->deliverFailedLater(tr("Unknown system voice \"%1\".").arg(voice.id));
        return stream;
    }
    if (text.trimmed().isEmpty()) {
        stream->deliverFailedLater(tr("The system voice produced no audio."));
        return stream;
    }
    d->ensureBackends();

    Job job;
    job.id = jobId;
    job.stream = stream;
    job.text = text;
    job.engine = voice.id.left(first);
    job.voiceName = voice.id.mid(first + 1, last - first - 1);
    job.localeName = voice.id.mid(last + 1);
    job.rate = qBound(-1.0, std::log2(qBound(0.5, options.rate, 2.0)), 1.0);
    job.pitch = qBound(-1.0, options.pitch, 1.0);
    d->queue.append(job);
    d->scheduleNext();
    return stream;
}

#else // Qt < 6.6: QTextToSpeech cannot hand out audio data.

struct SystemTtsEngine::Private
{
};

SystemTtsEngine::SystemTtsEngine(const EngineContext &context, QObject *parent)
    : TtsEngine(context, parent)
    , d(std::make_unique<Private>())
{
}

SystemTtsEngine::~SystemTtsEngine() = default;

bool SystemTtsEngine::isAvailable() const
{
    return false;
}

QString SystemTtsEngine::unavailableReason() const
{
    return tr("System voices need a Vocal Ink build made with Qt 6.6 or newer (this one uses Qt %1).")
        .arg(QLatin1String(QT_VERSION_STR));
}

void SystemTtsEngine::refreshVoices()
{
    setVoices({});
}

TtsStream *SystemTtsEngine::synthesize(const QString &text, const Voice &voice, const SpeakOptions &options)
{
    Q_UNUSED(text)
    Q_UNUSED(voice)
    Q_UNUSED(options)
    auto *stream = new BufferTtsStream;
    stream->deliverFailedLater(unavailableReason());
    return stream;
}

#endif
