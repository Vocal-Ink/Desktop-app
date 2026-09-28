#include "app/AppContext.h"

#include "audio/AudioPlayer.h"
#include "core/HistoryModel.h"
#include "core/Paths.h"
#include "core/PhraseStore.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "core/SpeechQueue.h"
#include "core/TextProcessor.h"
#include "models/ModelManager.h"
#include "obs/ObsIntegration.h"
#include "obs/OverlayServer.h"
#include "platform/GlobalHotkeys.h"
#include "stt/SttController.h"
#include "stt/SttEngines.h"
#include "tts/Engines.h"
#include "tts/TtsRegistry.h"

#include <QKeySequence>
#include <QNetworkAccessManager>
#include <QTimer>

namespace {
const QString kHotkeyPtt = QStringLiteral("ptt");
const QString kHotkeyStop = QStringLiteral("stop");
const QString kHotkeyQuickType = QStringLiteral("quickType");
const QString kHotkeyRepeat = QStringLiteral("repeat");
const QString kHotkeyPhrasePrefix = QStringLiteral("phrase:");
} // namespace

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
    m_settings = new Settings(this);
    m_secrets = new SecretStore(SecretStore::Backend::Keychain, this);
    m_network = new QNetworkAccessManager(this);
    m_tts = new TtsRegistry(this);
    m_player = new AudioPlayer(this);
    m_speech = new SpeechQueue(m_tts, m_player, this);
    m_history = new HistoryModel(this);
    m_phrases = new PhraseStore(Paths::phrasesFile(), this);
    m_stt = new SttController(this);
    m_models = new ModelManager(m_network, this);
    m_obs = new ObsIntegration(this);
    m_overlay = new OverlayServer(this);
    m_hotkeys = new GlobalHotkeys(this);

    connect(m_secrets, &SecretStore::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 1); });
    connect(m_player, &AudioPlayer::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 2); });
    connect(m_hotkeys, &GlobalHotkeys::pressed, this, &AppContext::onHotkeyPressed);
    connect(m_hotkeys, &GlobalHotkeys::released, this, &AppContext::onHotkeyReleased);
    connect(m_phrases, &PhraseStore::changed, this, &AppContext::applyHotkeys);
    connect(m_obs, &ObsIntegration::statusChanged, this, [this](ObsIntegration::Status status, const QString &text) {
        if (status == ObsIntegration::Status::AuthFailed || status == ObsIntegration::Status::Error)
            emit notify(tr("OBS: %1").arg(text), 1);
    });

    // Keep engines' voice lists and the installed-model state in sync.
    connect(m_models, &ModelManager::installedChanged, this, [this] {
        m_tts->refreshAll();
        if (m_sttEngine && m_sttEngine->id() == QLatin1String("whisper"))
            recreateSttEngine();
    });
    connect(m_secrets, &SecretStore::changed, this, [this](const QString &) {
        for (TtsEngine *e : m_tts->engines())
            emit e->availabilityChanged();
    });

    wireSpeech();
    wireStt();
}

AppContext::~AppContext()
{
    m_speech->stop();
    m_hotkeys->clear();
    m_settings->sync();
}

void AppContext::initialize()
{
    m_phrases->load();

    const EngineContext ctx{m_network, m_settings, m_secrets};
    const QList<TtsEngine *> engines = createTtsEngines(ctx, m_tts);
    for (TtsEngine *engine : engines) {
        m_tts->addEngine(engine);
        connect(engine, &TtsEngine::voicesError, this, [this, engine](const QString &msg) {
            emit notify(tr("%1: %2").arg(engine->displayName(), msg), 1);
        });
    }
    connect(m_tts, &TtsRegistry::voicesChanged, this, &AppContext::resolveVoiceWhenReady);

    connect(m_secrets, &SecretStore::loaded, this, [this] {
        // Engines that need keys become available once the keychain has answered.
        for (TtsEngine *e : m_tts->engines())
            emit e->availabilityChanged();
        m_tts->refreshAll();
        applyObsSettings();
        recreateSttEngine();
    });
    m_secrets->load();

    applyAudioRouting();
    applySpeechOptions();
    applySttSettings();
    applyOverlaySettings();
    applyHotkeys();
    m_tts->refreshAll();
}

void AppContext::wireSpeech()
{
    connect(m_speech, &SpeechQueue::queued, this, [this](quint64 id, const QString &text, const Voice &voice) {
        m_history->add(id, text, voice.name);
    });
    connect(m_speech, &SpeechQueue::started, this, [this](quint64 id, const QString &text, const Voice &voice) {
        m_history->setStatus(id, HistoryModel::Status::Speaking);
        m_obs->utteranceStarted(text);
        m_overlay->showCaption(id, text, voice.name);
    });
    connect(m_speech, &SpeechQueue::finished, this, [this](quint64 id, const QString &text, bool completed) {
        m_history->setStatus(id, completed ? HistoryModel::Status::Done : HistoryModel::Status::Stopped);
        m_obs->utteranceFinished(text);
        m_overlay->endCaption(id);
    });
    connect(m_speech, &SpeechQueue::failed, this, [this](quint64 id, const QString &text, const QString &error) {
        m_history->setStatus(id, HistoryModel::Status::Failed, error);
        m_obs->utteranceFinished(text);
        m_overlay->endCaption(id);
        emit notify(error, 2);
    });
    connect(m_speech, &SpeechQueue::speakingChanged, m_overlay, &OverlayServer::setSpeaking);
}

void AppContext::wireStt()
{
    connect(m_stt, &SttController::transcript, this, [this](const QString &text) {
        if (m_settings->flag(Keys::SttAutoSpeak))
            speak(text);
        else
            emit transcriptReady(text);
    });
    connect(m_stt, &SttController::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 2); });
    connect(m_stt, &SttController::listeningChanged, m_overlay, &OverlayServer::setListening);
}

quint64 AppContext::speak(const QString &text, const QString &voiceKey)
{
    const QString expanded = TextProcessor::expandReplacements(text, m_settings->value(Keys::Replacements).toMap());
    const Voice voice = voiceKey.isEmpty() ? Voice() : m_tts->resolve(voiceKey);
    return m_speech->say(expanded, voice);
}

void AppContext::stopSpeaking()
{
    m_speech->stop();
}

void AppContext::skipCurrent()
{
    m_speech->skip();
}

void AppContext::repeatLast()
{
    const QString last = m_history->lastText();
    if (!last.isEmpty())
        m_speech->say(last);
}

Voice AppContext::currentVoice() const
{
    return m_speech->voice();
}

void AppContext::setCurrentVoice(const Voice &voice)
{
    m_voiceResolved = true;
    m_speech->setVoice(voice);
    m_settings->setValue(Keys::Voice, voice.key());
    emit currentVoiceChanged(voice);
}

void AppContext::resolveVoiceWhenReady()
{
    const QString key = m_settings->string(Keys::Voice);
    const Voice current = m_speech->voice();
    if (!key.isEmpty()) {
        // Refresh the stored voice with full metadata once its engine lists it.
        const Voice resolved = m_tts->resolve(key);
        if (!current.isValid() || resolved.name != current.name) {
            m_speech->setVoice(resolved);
            emit currentVoiceChanged(resolved);
        }
        m_voiceResolved = true;
        return;
    }
    if (m_voiceResolved && current.isValid())
        return;
    const Voice fallback = m_tts->fallbackVoice();
    if (fallback.isValid()) {
        m_speech->setVoice(fallback);
        m_voiceResolved = true;
        emit currentVoiceChanged(fallback);
    }
}

void AppContext::applyAudioRouting()
{
    AudioPlayer::Routing r;
    r.mainDevice = m_settings->value(Keys::OutputDevice).toByteArray();
    r.mainGain = float(m_settings->integer(Keys::OutputVolume)) / 100.0f;
    r.monitorEnabled = m_settings->flag(Keys::MonitorEnabled);
    r.monitorDevice = m_settings->value(Keys::MonitorDevice).toByteArray();
    r.monitorGain = float(m_settings->integer(Keys::MonitorVolume)) / 100.0f;
    m_player->setRouting(r);
    m_stt->setInputDevice(m_settings->value(Keys::InputDevice).toByteArray());
}

void AppContext::applySpeechOptions()
{
    SpeakOptions o;
    o.rate = qBound(0.5, m_settings->integer(Keys::Rate) / 100.0, 2.0);
    o.pitch = qBound(-1.0, m_settings->integer(Keys::Pitch) / 50.0, 1.0);
    o.instructions = m_settings->string(Keys::OpenAiInstructions);
    m_speech->setOptions(o);
    m_speech->setSplitSentences(m_settings->flag(Keys::SplitSentences));
    const QString key = m_settings->string(Keys::Voice);
    if (!key.isEmpty() && m_speech->voice().key() != key)
        m_speech->setVoice(m_tts->resolve(key));
}

void AppContext::recreateSttEngine()
{
    const QString id = m_settings->string(Keys::SttEngine);
    SttEngine *old = m_sttEngine;
    const EngineContext ctx{m_network, m_settings, m_secrets};
    m_sttEngine = createSttEngine(id, ctx, this);
    m_stt->setEngine(m_sttEngine);
    if (old)
        old->deleteLater();
    applySttOptions();
}

void AppContext::applySttSettings()
{
    if (!m_sttEngine || m_sttEngine->id() != m_settings->string(Keys::SttEngine))
        recreateSttEngine();
    applySttOptions();
}

void AppContext::applySttOptions()
{
    m_stt->setMode(SttController::modeFromString(m_settings->string(Keys::SttMode)));
    m_stt->setVadSensitivity(m_settings->integer(Keys::SttVadSensitivity));
    m_stt->setInputDevice(m_settings->value(Keys::InputDevice).toByteArray());
    if (m_sttEngine) {
        SttEngine::Options o;
        o.language = m_settings->string(Keys::SttLanguage);
        o.prompt = m_settings->string(Keys::SttPrompt);
        m_sttEngine->setOptions(o);
    }
}

void AppContext::applyObsSettings()
{
    ObsIntegration::Config c;
    c.enabled = m_settings->flag(Keys::ObsEnabled);
    c.host = m_settings->string(Keys::ObsHost);
    c.port = quint16(m_settings->integer(Keys::ObsPort));
    c.password = m_secrets->get(Secrets::Obs);
    c.subtitles = m_settings->flag(Keys::ObsSubtitlesEnabled);
    c.subtitleSource = m_settings->string(Keys::ObsSubtitlesSource);
    c.clearAfterMs = m_settings->integer(Keys::ObsSubtitlesClearMs);
    c.captions = m_settings->flag(Keys::ObsCaptionsEnabled);
    c.indicator = m_settings->flag(Keys::ObsIndicatorEnabled);
    c.indicatorSource = m_settings->string(Keys::ObsIndicatorSource);
    m_obs->setConfig(c);
}

void AppContext::applyOverlaySettings()
{
    const bool enabled = m_settings->flag(Keys::OverlayEnabled);
    const quint16 port = quint16(m_settings->integer(Keys::OverlayPort));
    const bool lan = m_settings->flag(Keys::OverlayAllowLan);
    if (!enabled) {
        m_overlay->stop();
        return;
    }
    if (m_overlay->isRunning() && m_overlay->port() == port)
        m_overlay->stop(); // re-bind in case the LAN setting changed
    if (!m_overlay->start(port, lan))
        emit notify(tr("The OBS caption overlay could not start on port %1: %2").arg(port).arg(m_overlay->errorString()), 1);
}

QString AppContext::overlayUrl() const
{
    return m_overlay->overlayUrl(m_settings->string(Keys::OverlayQuery)).toString();
}

void AppContext::applyHotkeys()
{
    m_hotkeys->clear();
    if (!GlobalHotkeys::isSupported())
        return;
    const auto bind = [this](const QString &id, const QString &seq) {
        if (!seq.isEmpty() && !m_hotkeys->set(id, QKeySequence::fromString(seq, QKeySequence::PortableText)))
            emit notify(tr("The shortcut %1 is already used by another app.").arg(seq), 1);
    };
    bind(kHotkeyPtt, m_settings->string(Keys::HotkeyPushToTalk));
    bind(kHotkeyStop, m_settings->string(Keys::HotkeyStop));
    bind(kHotkeyQuickType, m_settings->string(Keys::HotkeyQuickType));
    bind(kHotkeyRepeat, m_settings->string(Keys::HotkeyRepeat));
    const QList<Phrase> &list = m_phrases->phrases();
    for (int i = 0; i < list.size(); ++i)
        bind(kHotkeyPhrasePrefix + QString::number(i), list.at(i).hotkey);
}

void AppContext::applyAll()
{
    applyAudioRouting();
    applySpeechOptions();
    applySttSettings();
    applyObsSettings();
    applyOverlaySettings();
    applyHotkeys();
}

void AppContext::onHotkeyPressed(const QString &id)
{
    if (id == kHotkeyPtt) {
        if (m_stt->mode() == SttController::Mode::PushToTalk)
            m_stt->startListening();
        else
            m_stt->toggleListening();
    } else if (id == kHotkeyStop) {
        stopSpeaking();
        m_stt->cancel();
    } else if (id == kHotkeyQuickType) {
        emit quickTypeRequested();
    } else if (id == kHotkeyRepeat) {
        repeatLast();
    } else if (id.startsWith(kHotkeyPhrasePrefix)) {
        const int index = id.mid(kHotkeyPhrasePrefix.size()).toInt();
        const QList<Phrase> &list = m_phrases->phrases();
        if (index >= 0 && index < list.size())
            speak(list.at(index).text, list.at(index).voiceKey);
    }
}

void AppContext::onHotkeyReleased(const QString &id)
{
    if (id == kHotkeyPtt && m_stt->mode() == SttController::Mode::PushToTalk)
        m_stt->stopListening();
}
