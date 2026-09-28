#pragma once

#include "tts/Voice.h"

#include <QObject>

class AppContext;
class AudioPlayer;
class QAudioOutput;
class QMediaPlayer;
class SpeechQueue;

// Plays voice samples to the user's own speakers/headphones only (never into
// the virtual cable), so browsing voices doesn't leak onto a stream or call.
class VoicePreview : public QObject
{
    Q_OBJECT
public:
    explicit VoicePreview(AppContext *context, QObject *parent = nullptr);
    ~VoicePreview() override;

    // Uses the provider's free sample when there is one, otherwise synthesizes `text`.
    void play(const Voice &voice, const QString &text, bool preferSample = true);
    void stop();

signals:
    void playingChanged(bool playing);
    void failed(const QString &message);

private:
    void configureOutput();

    AppContext *m_ctx;
    AudioPlayer *m_player;
    SpeechQueue *m_queue;
    QMediaPlayer *m_media = nullptr;
    QAudioOutput *m_mediaOut = nullptr;
};
