#include "ui/VoicePreview.h"

#include "app/AppContext.h"
#include "audio/AudioOutputLane.h"
#include "audio/AudioPlayer.h"
#include "core/Settings.h"
#include "core/SpeechQueue.h"
#include "tts/TtsRegistry.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

VoicePreview::VoicePreview(AppContext *context, QObject *parent)
    : QObject(parent)
    , m_ctx(context)
    , m_player(new AudioPlayer(this))
    , m_queue(new SpeechQueue(context->tts(), m_player, this))
{
    m_queue->setSplitSentences(false);
    connect(m_queue, &SpeechQueue::speakingChanged, this, &VoicePreview::playingChanged);
    connect(m_queue, &SpeechQueue::failed, this, [this](quint64, const QString &, const QString &error) {
        emit failed(error);
    });
}

VoicePreview::~VoicePreview()
{
    stop();
}

void VoicePreview::configureOutput()
{
    Settings *s = m_ctx->settings();
    AudioPlayer::Routing r;
    // The monitor device is "my headphones"; fall back to the system default.
    r.mainDevice = s->value(Keys::MonitorDevice).toByteArray();
    r.mainGain = qMax(0.3f, float(s->integer(Keys::MonitorVolume)) / 100.0f);
    r.monitorEnabled = false;
    m_player->setRouting(r);
}

void VoicePreview::play(const Voice &voice, const QString &text, bool preferSample)
{
    stop();
    configureOutput();
    if (preferSample && !voice.previewUrl.isEmpty()) {
        if (!m_media) {
            m_media = new QMediaPlayer(this);
            m_mediaOut = new QAudioOutput(this);
            m_media->setAudioOutput(m_mediaOut);
            connect(m_media, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
                emit playingChanged(st == QMediaPlayer::PlayingState);
            });
            connect(m_media, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
                emit failed(tr("Could not play the sample: %1").arg(msg));
            });
        }
        m_mediaOut->setDevice(QtAudioLane::findOutputDevice(m_ctx->settings()->value(Keys::MonitorDevice).toByteArray()));
        m_mediaOut->setVolume(float(m_ctx->settings()->integer(Keys::MonitorVolume)) / 100.0f);
        m_media->setSource(QUrl(voice.previewUrl));
        m_media->play();
        return;
    }
    SpeakOptions o = m_ctx->speech()->options();
    m_queue->setOptions(o);
    m_queue->say(text, voice);
}

void VoicePreview::stop()
{
    m_queue->stop();
    if (m_media)
        m_media->stop();
}
