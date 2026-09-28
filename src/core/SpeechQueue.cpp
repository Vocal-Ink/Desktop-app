#include "core/SpeechQueue.h"

#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "core/TextProcessor.h"
#include "tts/TtsEngine.h"
#include "tts/TtsRegistry.h"

#include <QTimer>

SpeechQueue::SpeechQueue(TtsRegistry *registry, AudioPlayer *player, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_player(player)
{
    connect(m_player, &AudioPlayer::drained, this, &SpeechQueue::onDrained);
}

SpeechQueue::~SpeechQueue()
{
    cancelStreams();
}

void SpeechQueue::setEffect(const QString &effectId, float intensity)
{
    // Stored here; the audio work package applies it to the stream in feed().
    m_effectId = effectId;
    m_effectIntensity = intensity;
}

quint64 SpeechQueue::say(const QString &text, const Voice &voiceOverride)
{
    const QString normalized = TextProcessor::normalizeForSpeech(text);
    if (normalized.isEmpty())
        return 0;

    Job job;
    job.id = m_nextId++;
    job.text = normalized;
    job.voice = voiceOverride.isValid() ? voiceOverride : m_voice;
    if (!job.voice.isValid() || !m_registry->isUsable(job.voice)) {
        const Voice fallback = m_registry->fallbackVoice();
        if (fallback.isValid() && !job.voice.isValid())
            job.voice = fallback;
    }
    job.options = m_options;
    job.chunks = m_split ? TextProcessor::splitForSpeech(normalized) : QStringList{normalized};

    if (m_interrupt && (m_current || !m_queue.isEmpty()))
        stop(); // the newest message wins

    m_queue.enqueue(job);
    emit queued(job.id, job.text, job.voice);
    emit queueChanged(int(m_queue.size()));
    if (!m_current)
        startNext();
    return job.id;
}

void SpeechQueue::startNext()
{
    while (!m_current && !m_queue.isEmpty()) {
        Job job = m_queue.dequeue();
        emit queueChanged(int(m_queue.size()));

        TtsEngine *engine = m_registry->engine(job.voice.engineId);
        if (!job.voice.isValid() || !engine) {
            emit failed(job.id, job.text,
                        tr("No voice is selected. Pick a voice in the toolbar, or install one in Settings → Voices."));
            continue;
        }
        if (!engine->isAvailable()) {
            const QString reason = engine->unavailableReason();
            emit failed(job.id, job.text,
                        reason.isEmpty() ? tr("%1 is not set up yet.").arg(engine->displayName()) : reason);
            continue;
        }

        m_current = job;
        m_chunks = QVector<Chunk>(job.chunks.size());
        m_playIndex = 0;
        m_playerRate = 0;
        m_announced = false;
        m_feedingDone = false;
        m_error.clear();
        setSpeaking(true);

        startChunk(0);
        startChunk(1);
        return;
    }
    if (!m_current)
        setSpeaking(false);
}

void SpeechQueue::setSpeaking(bool speaking)
{
    if (m_speaking == speaking)
        return;
    m_speaking = speaking;
    emit speakingChanged(speaking);
}

void SpeechQueue::startChunk(int index)
{
    if (!m_current || index < 0 || index >= m_chunks.size() || m_chunks[index].started)
        return;
    m_chunks[index].started = true;

    TtsEngine *engine = m_registry->engine(m_current->voice.engineId);
    TtsStream *stream = engine
        ? engine->synthesize(m_current->chunks.at(index), m_current->voice, m_current->options)
        : nullptr;
    if (!stream) {
        const quint64 id = m_current->id;
        QTimer::singleShot(0, this, [this, id, index] {
            if (m_current && m_current->id == id)
                onChunkFailed(index, tr("The voice engine could not start."));
        });
        return;
    }
    stream->setParent(this);
    m_chunks[index].stream = stream;

    const quint64 id = m_current->id;
    auto isLive = [this, id, index, stream] {
        return m_current && m_current->id == id && index < m_chunks.size() && m_chunks[index].stream == stream;
    };
    connect(stream, &TtsStream::audioReady, this, [this, isLive, index](const QAudioFormat &fmt, const QByteArray &pcm) {
        if (isLive())
            onAudio(index, fmt, pcm);
    });
    connect(stream, &TtsStream::finished, this, [this, isLive, index, stream] {
        stream->deleteLater();
        if (isLive())
            onChunkFinished(index);
    });
    connect(stream, &TtsStream::failed, this, [this, isLive, index, stream](const QString &error) {
        stream->deleteLater();
        if (isLive())
            onChunkFailed(index, error);
    });
}

void SpeechQueue::onAudio(int index, const QAudioFormat &format, const QByteArray &pcm)
{
    const QVector<float> mono = AudioConvert::toMonoFloat(pcm, format);
    if (mono.isEmpty())
        return;
    if (index == m_playIndex) {
        feed(mono, format.sampleRate());
    } else {
        Chunk &c = m_chunks[index];
        c.buffered += mono;
        c.rate = format.sampleRate();
    }
}

void SpeechQueue::feed(const QVector<float> &mono, int rate)
{
    if (rate != m_playerRate) {
        m_player->setSourceRate(rate);
        m_playerRate = rate;
    }
    m_player->write(mono);
    if (!m_announced && m_current) {
        m_announced = true;
        emit started(m_current->id, m_current->text, m_current->voice);
    }
}

void SpeechQueue::onChunkFinished(int index)
{
    m_chunks[index].done = true;
    m_chunks[index].stream = nullptr;
    if (index == m_playIndex)
        advance();
}

void SpeechQueue::advance()
{
    while (true) {
        ++m_playIndex;
        if (m_playIndex >= m_chunks.size()) {
            m_feedingDone = true;
            m_player->finish();
            return;
        }
        startChunk(m_playIndex);
        startChunk(m_playIndex + 1); // keep one chunk ahead
        Chunk &c = m_chunks[m_playIndex];
        if (!c.buffered.isEmpty()) {
            const QVector<float> data = std::move(c.buffered);
            c.buffered.clear();
            feed(data, c.rate);
        }
        if (!c.done)
            return;
    }
}

void SpeechQueue::onChunkFailed(int index, const QString &error)
{
    Q_UNUSED(index)
    if (!m_current || m_feedingDone)
        return;
    m_error = error;
    cancelStreams();
    m_feedingDone = true;
    m_player->finish(); // let whatever already arrived play out
}

void SpeechQueue::onDrained()
{
    if (!m_current || !m_feedingDone)
        return;
    endCurrent();
}

void SpeechQueue::endCurrent()
{
    const Job job = *m_current;
    m_current.reset();
    m_chunks.clear();
    const QString error = m_error;
    m_error.clear();
    if (!error.isEmpty())
        emit failed(job.id, job.text, error);
    else
        emit finished(job.id, job.text, true);
    if (m_queue.isEmpty()) {
        setSpeaking(false);
        return;
    }
    QTimer::singleShot(0, this, &SpeechQueue::startNext);
}

void SpeechQueue::cancelStreams()
{
    for (Chunk &c : m_chunks) {
        if (c.stream) {
            c.stream->cancel();
            c.stream->deleteLater();
            c.stream = nullptr;
        }
    }
}

void SpeechQueue::skip()
{
    if (!m_current)
        return;
    cancelStreams();
    m_player->stop();
    const Job job = *m_current;
    m_current.reset();
    m_chunks.clear();
    m_error.clear();
    emit finished(job.id, job.text, false);
    startNext();
}

void SpeechQueue::stop()
{
    const QQueue<Job> dropped = m_queue;
    m_queue.clear();
    if (m_current) {
        cancelStreams();
        m_player->stop();
        const Job job = *m_current;
        m_current.reset();
        m_chunks.clear();
        m_error.clear();
        emit finished(job.id, job.text, false);
    }
    for (const Job &j : dropped)
        emit finished(j.id, j.text, false);
    if (!dropped.isEmpty())
        emit queueChanged(0);
    setSpeaking(false);
}
