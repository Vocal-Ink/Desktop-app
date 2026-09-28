#pragma once

#include "tts/Voice.h"

#include <QAudioFormat>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QVector>
#include <optional>

class AudioPlayer;
class TtsRegistry;
class TtsStream;

// Turns messages into speech one after another. Long messages are split into
// sentence-sized chunks: chunk N+1 is synthesised while chunk N plays, so the
// listener hears the first words as soon as possible.
class SpeechQueue : public QObject
{
    Q_OBJECT
public:
    SpeechQueue(TtsRegistry *registry, AudioPlayer *player, QObject *parent = nullptr);
    ~SpeechQueue() override;

    void setSplitSentences(bool split) { m_split = split; }
    void setVoice(const Voice &voice) { m_voice = voice; }
    Voice voice() const { return m_voice; }
    void setOptions(const SpeakOptions &options) { m_options = options; }
    SpeakOptions options() const { return m_options; }
    // Voice effect applied to everything spoken (see audio/VoiceEffects.h).
    // `effectId` is VoiceEffects::id(); intensity 0..1.
    void setEffect(const QString &effectId, float intensity);
    // When true, a new message interrupts the current one instead of queuing.
    void setInterrupt(bool interrupt) { m_interrupt = interrupt; }

    // Queues text (already expanded/normalised by the caller). Returns the
    // message id, or 0 if there is nothing to say.
    quint64 say(const QString &text, const Voice &voiceOverride = {});

    void stop();  // stops the current message and drops everything queued
    void skip();  // stops the current message; the queue continues
    bool isSpeaking() const { return m_current.has_value(); }
    int queuedCount() const { return int(m_queue.size()); }

signals:
    void queued(quint64 id, const QString &text, const Voice &voice);
    void started(quint64 id, const QString &text, const Voice &voice); // first audio is playing
    void finished(quint64 id, const QString &text, bool completed);    // completed=false when stopped
    void failed(quint64 id, const QString &text, const QString &error);
    void speakingChanged(bool speaking);
    void queueChanged(int queued);

private:
    struct Job
    {
        quint64 id = 0;
        QString text;
        Voice voice;
        SpeakOptions options;
        QStringList chunks;
    };
    struct Chunk
    {
        QPointer<TtsStream> stream;
        QVector<float> buffered;
        int rate = 0;
        bool started = false;
        bool done = false;
    };

    void startNext();
    void startChunk(int index);
    void onAudio(int index, const QAudioFormat &format, const QByteArray &pcm);
    void onChunkFinished(int index);
    void onChunkFailed(int index, const QString &error);
    void feed(const QVector<float> &mono, int rate);
    void advance();
    void onDrained();
    void cancelStreams();
    void endCurrent();
    void setSpeaking(bool speaking);

    TtsRegistry *m_registry;
    AudioPlayer *m_player;
    bool m_split = true;
    Voice m_voice;
    SpeakOptions m_options;
    quint64 m_nextId = 1;

    QQueue<Job> m_queue;
    std::optional<Job> m_current;
    QVector<Chunk> m_chunks;
    int m_playIndex = 0;     // chunk whose audio goes to the player
    int m_playerRate = 0;
    bool m_announced = false; // started() emitted for m_current
    bool m_feedingDone = false;
    bool m_speaking = false;
    bool m_interrupt = false;
    QString m_effectId;
    float m_effectIntensity = 0.0f;
    QString m_error;
};
