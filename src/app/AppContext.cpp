#include "app/AppContext.h"

#include "audio/AudioPlayer.h"
#include "audio/Earcons.h"
#include "audio/MicPassthrough.h"
#include "audio/Soundboard.h"
#include "audio/VoiceEffects.h"
#include "core/ActionRegistry.h"
#include "core/HistoryModel.h"
#include "core/Paths.h"
#include "core/PhraseStore.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "core/SpeechQueue.h"
#include "core/TextProcessor.h"
#include "core/UpdateChecker.h"
#include "core/VoicePresets.h"
#include "core/WordPredictor.h"
#include "models/ModelManager.h"
#include "obs/ObsIntegration.h"
#include "obs/OverlayServer.h"
#include "obs/TwitchChat.h"
#include "platform/GlobalHotkeys.h"
#include "platform/VirtualDriver.h"
#include "stt/SttController.h"
#include "stt/SttEngines.h"
#include "tts/Engines.h"
#include "tts/TtsRegistry.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeySequence>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QTimer>

namespace {
const QString kActionPrefix = QStringLiteral("action:");
const QString kPhrasePrefix = QStringLiteral("phrase:");
const QString kSoundPrefix = QStringLiteral("sound:");

// Which apply*() a settings key belongs to.
QString areaOf(const QString &key)
{
    if (key.startsWith(QLatin1String("keybinds/")) || key.startsWith(QLatin1String("hotkeys/")))
        return QStringLiteral("hotkeys");
    if (key.startsWith(QLatin1String("audio/")))
        return QStringLiteral("audio");
    if (key.startsWith(QLatin1String("tts/")) || key.startsWith(QLatin1String("fx/"))
        || key == QLatin1String(Keys::Interrupt))
        return QStringLiteral("speech");
    if (key.startsWith(QLatin1String("stt/")))
        return QStringLiteral("stt");
    if (key.startsWith(QLatin1String("obs/")))
        return QStringLiteral("obs");
    if (key.startsWith(QLatin1String("overlay/")))
        return QStringLiteral("overlay");
    if (key.startsWith(QLatin1String("mic/")))
        return QStringLiteral("mic");
    if (key.startsWith(QLatin1String("a11y/soundCue")))
        return QStringLiteral("cues");
    if (key.startsWith(QLatin1String("twitch/")))
        return QStringLiteral("twitch");
    return {};
}

QStringList splitList(const QString &text)
{
    QStringList out;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,\\n]")), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}
} // namespace

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
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
    m_actions = new ActionRegistry(m_settings, this);
    m_mic = new MicPassthrough(m_player, this);
    m_soundboard = new Soundboard(m_player, Paths::ensureDir(Paths::dataDir() + QStringLiteral("/sounds")), this);
    m_earcons = new Earcons(this);
    m_predictor = new WordPredictor(Paths::dataDir() + QStringLiteral("/words.json"), this);
    m_presets = new VoicePresets(Paths::dataDir() + QStringLiteral("/presets.json"), this);
    m_updates = new UpdateChecker(m_network, this);
    m_twitch = new TwitchChat(this);
    m_driver = new VirtualDriver(this);

    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(150);
    connect(m_applyTimer, &QTimer::timeout, this, [this] {
        const QSet<QString> areas = std::exchange(m_pendingAreas, {});
        if (areas.contains(QStringLiteral("audio"))) {
            applyAudioRouting();
            applyCueSettings();
        }
        if (areas.contains(QStringLiteral("speech")))
            applySpeechOptions();
        if (areas.contains(QStringLiteral("stt")))
            applySttSettings();
        if (areas.contains(QStringLiteral("obs")))
            applyObsSettings();
        if (areas.contains(QStringLiteral("overlay")))
            applyOverlaySettings();
        if (areas.contains(QStringLiteral("mic")))
            applyMicSettings();
        if (areas.contains(QStringLiteral("cues")))
            applyCueSettings();
        if (areas.contains(QStringLiteral("twitch")))
            applyTwitchSettings();
        if (areas.contains(QStringLiteral("hotkeys")))
            applyHotkeys();
    });
    connect(m_settings, &Settings::changed, this, &AppContext::onSettingChanged);

    connect(m_secrets, &SecretStore::errorOccurred, this, [this](const QString &msg) {
        // The keychain fallback is a lasting condition: say it once, not every launch.
        const QStringList seen = m_settings->value(QStringLiteral("app/seenNotices"), QStringList()).toStringList();
        const QString id = QString::number(qHash(msg));
        if (seen.contains(id))
            return;
        m_settings->setValue(QStringLiteral("app/seenNotices"), QStringList(seen) << id);
        emit notify(msg, 1);
    });
    connect(m_player, &AudioPlayer::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 2); });
    connect(m_hotkeys, &GlobalHotkeys::pressed, this, &AppContext::onHotkeyPressed);
    connect(m_hotkeys, &GlobalHotkeys::released, this, &AppContext::onHotkeyReleased);
    connect(m_phrases, &PhraseStore::changed, this, &AppContext::applyHotkeys);
    connect(m_soundboard, &Soundboard::changed, this, &AppContext::applyHotkeys);
    connect(m_actions, &ActionRegistry::shortcutsReset, this, &AppContext::applyHotkeys);
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
    connect(m_secrets, &SecretStore::changed, this, [this](const QString &name) {
        for (TtsEngine *e : m_tts->engines())
            emit e->availabilityChanged();
        if (name == Secrets::Obs)
            applyObsSettings();
        if (name == Secrets::OpenAiStt)
            recreateSttEngine();
    });

    wireSpeech();
    wireStt();
    wireExtras();
}

AppContext::~AppContext()
{
    m_speech->stop();
    m_soundboard->stopAll();
    m_mic->setLive(false);
    m_hotkeys->clear();
    m_predictor->save();
    m_settings->sync();
}

void AppContext::initialize()
{
    // Shortcuts from before the keybind registry existed.
    const QPair<const char *, QString> legacy[] = {
        {Keys::HotkeyPushToTalk, QStringLiteral("listen.ptt")},
        {Keys::HotkeyStop, QStringLiteral("speak.stop")},
        {Keys::HotkeyQuickType, QStringLiteral("window.quickType")},
        {Keys::HotkeyRepeat, QStringLiteral("speak.repeat")},
    };
    for (const auto &[oldKey, action] : legacy) {
        if (m_settings->contains(QString::fromLatin1(oldKey))) {
            m_actions->setShortcut(action, m_settings->string(oldKey));
            m_settings->remove(QString::fromLatin1(oldKey));
        }
    }

    m_phrases->load();
    m_soundboard->load();
    m_predictor->load();
    m_presets->load();

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
    applyMicSettings();
    applyCueSettings();
    applyTwitchSettings();
    applyHotkeys();
    m_tts->refreshAll();
    m_initialized = true;

    if (m_settings->flag(Keys::CheckUpdates))
        QTimer::singleShot(8000, m_updates, &UpdateChecker::check);
}

void AppContext::onSettingChanged(const QString &key)
{
    if (!m_initialized)
        return;
    const QString area = areaOf(key);
    if (area.isEmpty())
        return;
    m_pendingAreas.insert(area);
    m_applyTimer->start();
}

void AppContext::wireSpeech()
{
    connect(m_speech, &SpeechQueue::queued, this, [this](quint64 id, const QString &text, const Voice &voice) {
        m_history->add(id, text, voice.name);
    });
    connect(m_speech, &SpeechQueue::started, this, [this](quint64 id, const QString &text, const Voice &voice) {
        m_history->setStatus(id, HistoryModel::Status::Speaking);
        if (m_captionsPaused)
            return;
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
        m_earcons->play(Earcons::Cue::Error);
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
    connect(m_stt, &SttController::errorOccurred, this, [this](const QString &msg) {
        m_earcons->play(Earcons::Cue::Error);
        emit notify(msg, 2);
    });
    connect(m_stt, &SttController::listeningChanged, m_overlay, &OverlayServer::setListening);
    connect(m_stt, &SttController::listeningChanged, this, [this](bool listening) {
        m_earcons->play(listening ? Earcons::Cue::ListenStart : Earcons::Cue::ListenStop);
        if (!listening)
            m_pttLatched = false;
    });
}

void AppContext::wireExtras()
{
    connect(m_mic, &MicPassthrough::liveChanged, this, [this](bool live) {
        if (m_settings->flag(Keys::MicWarnSound))
            m_earcons->play(live ? Earcons::Cue::MicLive : Earcons::Cue::MicMuted);
        emit micLiveChanged(live, std::exchange(m_micFromShortcut, false));
    });
    connect(m_mic, &MicPassthrough::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 2); });
    connect(m_soundboard, &Soundboard::errorOccurred, this, [this](const QString &msg) { emit notify(msg, 1); });

    connect(m_twitch, &TwitchChat::speakRequested, this, [this](const QString &text, const TwitchChat::Message &) {
        // Chat is untrusted: no {variables} (a viewer could ask for {clipboard}),
        // no learning into word predictions.
        QString t = TextProcessor::expandReplacements(text, m_settings->value(Keys::Replacements).toMap());
        t = TextProcessor::handleEmoji(t, TextProcessor::EmojiMode::Speak);
        t = TextProcessor::handleUrls(t, TextProcessor::UrlMode::SayLink);
        const QString voiceKey = m_settings->string(Keys::TwitchVoice);
        const Voice voice = voiceKey.isEmpty() ? Voice() : m_tts->resolve(voiceKey);
        m_speech->say(t, voice);
    });
    connect(m_twitch, &TwitchChat::statusChanged, this, [this](bool connected, const QString &status) {
        if (!connected && m_settings->flag(Keys::TwitchEnabled) && !status.isEmpty())
            emit notify(tr("Twitch: %1").arg(status), 1);
    });

    connect(m_driver, &VirtualDriver::finished, this, [this](bool ok, const QString &message) {
        emit notify(message, ok ? 0 : 2);
        if (!ok)
            return;
        const QByteArray id = m_driver->outputDeviceId();
        if (!id.isEmpty() && m_settings->value(Keys::OutputDevice).toByteArray().isEmpty())
            m_settings->setValue(Keys::OutputDevice, id);
    });
}

QString AppContext::prepareText(const QString &text) const
{
    TextProcessor::VariableContext vars;
    vars.custom = m_settings->value(Keys::Variables).toMap();
    if (text.contains(QLatin1String("{clipboard}"), Qt::CaseInsensitive) && QGuiApplication::clipboard())
        vars.clipboard = QGuiApplication::clipboard()->text();
    vars.voiceName = m_speech->voice().name;
    vars.now = QDateTime::currentDateTime();

    QString t = TextProcessor::expandVariables(text, vars);
    t = TextProcessor::expandReplacements(t, m_settings->value(Keys::Replacements).toMap());

    const QString emoji = m_settings->string(Keys::EmojiMode);
    t = TextProcessor::handleEmoji(t, emoji == QLatin1String("remove") ? TextProcessor::EmojiMode::Remove
                                      : emoji == QLatin1String("keep") ? TextProcessor::EmojiMode::Keep
                                                                       : TextProcessor::EmojiMode::Speak);
    const QString urls = m_settings->string(Keys::UrlMode);
    t = TextProcessor::handleUrls(t, urls == QLatin1String("remove") ? TextProcessor::UrlMode::Remove
                                     : urls == QLatin1String("keep") ? TextProcessor::UrlMode::Keep
                                                                     : TextProcessor::UrlMode::SayLink);
    if (m_settings->flag(Keys::AutoCapitalize))
        t = TextProcessor::autoCapitalize(t);
    return t.trimmed();
}

quint64 AppContext::speak(const QString &text, const QString &voiceKey)
{
    const QString prepared = prepareText(text);
    if (prepared.isEmpty())
        return 0;
    m_predictor->learn(text);
    const Voice voice = voiceKey.isEmpty() ? Voice() : m_tts->resolve(voiceKey);
    const quint64 id = m_speech->say(prepared, voice);
    if (id)
        m_earcons->play(Earcons::Cue::Sent);
    return id;
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

void AppContext::panic()
{
    m_speech->stop();
    m_soundboard->stopAll();
    m_stt->cancel();
    if (m_mic->isLive())
        m_mic->setLive(false);
    emit notify(tr("Everything stopped. Your real mic is muted."), 0);
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

void AppContext::cycleVoice(int delta)
{
    QStringList keys = m_settings->value(Keys::FavoriteVoices).toStringList();
    if (keys.isEmpty()) {
        const QList<Voice> all = m_tts->allVoices();
        for (const Voice &v : all) {
            if (m_tts->isUsable(v))
                keys << v.key();
        }
    }
    if (keys.isEmpty())
        return;
    const int current = int(keys.indexOf(m_speech->voice().key()));
    const int next = int((current + delta + keys.size() * 4) % keys.size());
    const Voice voice = m_tts->resolve(keys.at(next));
    setCurrentVoice(voice);
    emit notify(tr("Voice: %1").arg(voice.name), 0);
}

void AppContext::applyPreset(const QString &presetId)
{
    const VoicePreset p = m_presets->preset(presetId);
    if (p.id.isEmpty())
        return;
    if (!p.voiceKey.isEmpty())
        setCurrentVoice(m_tts->resolve(p.voiceKey));
    m_settings->setValue(Keys::Rate, p.rate);
    m_settings->setValue(Keys::Pitch, p.pitch);
    m_settings->setValue(Keys::Effect, p.effect.isEmpty() ? QStringLiteral("none") : p.effect);
    m_settings->setValue(Keys::EffectIntensity, p.effectIntensity);
    emit notify(tr("Preset: %1").arg(p.name), 0);
}

void AppContext::setCaptionsPaused(bool paused)
{
    if (m_captionsPaused == paused)
        return;
    m_captionsPaused = paused;
    if (paused)
        m_overlay->clearCaptions();
    emit captionsPausedChanged(paused);
    emit notify(paused ? tr("Captions paused") : tr("Captions back on"), 0);
}

void AppContext::adjustSetting(const char *key, int delta, int min, int max, const QString &label)
{
    const int value = qBound(min, m_settings->integer(key) + delta, max);
    m_settings->setValue(key, value);
    emit notify(label.arg(value), 0);
}

void AppContext::triggerAction(const QString &id, bool pressed)
{
    const ActionDef def = m_actions->action(id);
    if (!pressed && !def.hold)
        return;

    if (id == QLatin1String("speak.stop")) {
        stopSpeaking();
    } else if (id == QLatin1String("speak.skip")) {
        skipCurrent();
    } else if (id == QLatin1String("speak.repeat")) {
        repeatLast();
    } else if (id == QLatin1String("speak.clipboard")) {
        if (QGuiApplication::clipboard())
            speak(QGuiApplication::clipboard()->text());
    } else if (id == QLatin1String("listen.ptt")) {
        if (m_settings->flag(Keys::LatchPtt)) {
            if (pressed) {
                m_pttLatched = !m_stt->isListening();
                m_pttLatched ? m_stt->startListening() : m_stt->stopListening();
            }
        } else if (pressed) {
            m_stt->mode() == SttController::Mode::PushToTalk ? m_stt->startListening() : m_stt->toggleListening();
        } else if (m_stt->mode() == SttController::Mode::PushToTalk) {
            m_stt->stopListening();
        }
    } else if (id == QLatin1String("listen.toggle")) {
        m_stt->toggleListening();
    } else if (id == QLatin1String("listen.cancel")) {
        m_stt->cancel();
    } else if (id == QLatin1String("mic.hold") || id == QLatin1String("mic.toggle")) {
        if (m_mic->mode() == MicPassthrough::Mode::Off) {
            if (pressed)
                emit notify(tr("Real-mic passthrough is off. Turn it on in Audio → Your real microphone."), 1);
            return;
        }
        m_micFromShortcut = true;
        if (id == QLatin1String("mic.toggle"))
            m_mic->toggle();
        else if (pressed)
            m_mic->press();
        else
            m_mic->release();
    } else if (id == QLatin1String("panic.mute")) {
        panic();
    } else if (id == QLatin1String("voice.next")) {
        cycleVoice(1);
    } else if (id == QLatin1String("voice.prev")) {
        cycleVoice(-1);
    } else if (id == QLatin1String("rate.up")) {
        adjustSetting(Keys::Rate, 10, 50, 200, tr("Speed %1%"));
    } else if (id == QLatin1String("rate.down")) {
        adjustSetting(Keys::Rate, -10, 50, 200, tr("Speed %1%"));
    } else if (id == QLatin1String("volume.up")) {
        adjustSetting(Keys::OutputVolume, 10, 0, 100, tr("Voice volume %1%"));
    } else if (id == QLatin1String("volume.down")) {
        adjustSetting(Keys::OutputVolume, -10, 0, 100, tr("Voice volume %1%"));
    } else if (id == QLatin1String("effect.cycle")) {
        const QList<VoiceEffects::Effect> all = VoiceEffects::all();
        const int current = int(all.indexOf(VoiceEffects::fromId(m_settings->string(Keys::Effect))));
        const VoiceEffects::Effect next = all.at((current + 1) % all.size());
        m_settings->setValue(Keys::Effect, VoiceEffects::id(next));
        emit notify(tr("Effect: %1").arg(VoiceEffects::displayName(next)), 0);
    } else if (id == QLatin1String("stream.clearCaptions")) {
        m_overlay->clearCaptions();
    } else if (id == QLatin1String("stream.toggleCaptions")) {
        setCaptionsPaused(!m_captionsPaused);
    } else if (id.startsWith(QLatin1String("voice.fav"))) {
        const int n = id.mid(9).toInt() - 1;
        const QStringList favorites = m_settings->value(Keys::FavoriteVoices).toStringList();
        if (n >= 0 && n < favorites.size()) {
            const Voice voice = m_tts->resolve(favorites.at(n));
            setCurrentVoice(voice);
            emit notify(tr("Voice: %1").arg(voice.name), 0);
        } else {
            emit notify(tr("No favourite voice #%1 yet. Star voices in Voices.").arg(n + 1), 1);
        }
    } else if (id.startsWith(QLatin1String("preset."))) {
        const int n = id.mid(7).toInt() - 1;
        const QList<VoicePreset> &list = m_presets->presets();
        if (n >= 0 && n < list.size())
            applyPreset(list.at(n).id);
        else
            emit notify(tr("No preset #%1 yet. Save one in Voices → Presets.").arg(n + 1), 1);
    } else if (id == QLatin1String("window.quickType")) {
        emit quickTypeRequested();
        emit uiActionRequested(id);
    } else if (id == QLatin1String("window.toggle")) {
        emit uiActionRequested(id);
    } else {
        emit uiActionRequested(id);
    }
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
    m_speech->setEffect(m_settings->string(Keys::Effect), float(m_settings->integer(Keys::EffectIntensity)) / 100.0f);
    m_speech->setInterrupt(m_settings->flag(Keys::Interrupt));
    const QString key = m_settings->string(Keys::Voice);
    if (!key.isEmpty() && m_speech->voice().key() != key) {
        const Voice voice = m_tts->resolve(key);
        m_speech->setVoice(voice);
        emit currentVoiceChanged(voice);
    }
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
    if (m_overlay->isRunning())
        m_overlay->stop(); // re-bind in case the port or LAN setting changed
    if (!m_overlay->start(port, lan))
        emit notify(tr("The OBS caption overlay could not start on port %1: %2").arg(port).arg(m_overlay->errorString()), 1);
}

QString AppContext::overlayUrl() const
{
    return m_overlay->overlayUrl(m_settings->string(Keys::OverlayQuery)).toString();
}

void AppContext::applyMicSettings()
{
    m_mic->setInputDevice(m_settings->value(Keys::MicDevice).toByteArray());
    m_mic->setGainDb(float(m_settings->integer(Keys::MicGainDb)));
    m_mic->setGateDb(float(m_settings->integer(Keys::MicGateDb)));
    m_mic->setDuckDuringSpeech(m_settings->flag(Keys::MicDuck), float(m_settings->integer(Keys::MicDuckDb)));
    m_mic->setMode(MicPassthrough::modeFromString(m_settings->string(Keys::MicMode)));
}

void AppContext::applyCueSettings()
{
    m_earcons->setEnabled(m_settings->flag(Keys::SoundCues));
    m_earcons->setVolume(float(m_settings->integer(Keys::SoundCueVolume)) / 100.0f);
    // Cues are for the user only: never into the virtual cable.
    m_earcons->setDevice(m_settings->flag(Keys::MonitorEnabled) ? m_settings->value(Keys::MonitorDevice).toByteArray()
                                                                : QByteArray());
}

void AppContext::applyTwitchSettings()
{
    TwitchChat::Filter f;
    f.readUserName = m_settings->flag(Keys::TwitchReadNames);
    f.skipCommands = m_settings->flag(Keys::TwitchSkipCommands);
    f.skipLinks = m_settings->flag(Keys::TwitchSkipLinks);
    f.subscribersOnly = m_settings->flag(Keys::TwitchSubsOnly);
    f.ignoredUsers = splitList(m_settings->string(Keys::TwitchIgnored).toLower());
    f.blockedWords = splitList(m_settings->string(Keys::TwitchBlocked));
    m_twitch->setFilter(f);

    const QString channel = m_settings->string(Keys::TwitchChannel).trimmed().toLower().remove(QLatin1Char('#'));
    if (m_settings->flag(Keys::TwitchEnabled) && !channel.isEmpty()) {
        if (!m_twitch->isConnected() || m_twitch->channel() != channel)
            m_twitch->connectTo(channel);
    } else {
        m_twitch->disconnectFrom();
    }
}

void AppContext::setHotkeysSuspended(bool suspended)
{
    if (m_hotkeysSuspended == suspended)
        return;
    m_hotkeysSuspended = suspended;
    if (suspended)
        m_hotkeys->clear();
    else
        applyHotkeys();
}

void AppContext::applyHotkeys()
{
    // Tell the registry about phrase/sound shortcuts so it can flag clashes.
    QList<QPair<QString, QString>> extra;
    const QList<Phrase> &phrases = m_phrases->phrases();
    for (const Phrase &p : phrases) {
        if (!p.hotkey.isEmpty())
            extra.append({tr("Phrase “%1”").arg(p.text.left(24)), p.hotkey});
    }
    const QList<Sound> &sounds = m_soundboard->sounds();
    for (const Sound &s : sounds) {
        if (!s.hotkey.isEmpty())
            extra.append({tr("Sound “%1”").arg(s.name), s.hotkey});
    }
    m_actions->setExtraBindings(extra);

    m_hotkeys->clear();
    if (m_hotkeysSuspended || !GlobalHotkeys::isSupported())
        return;

    QStringList failed;
    const auto bind = [&](const QString &id, const QString &seq, const QString &label) {
        if (seq.isEmpty())
            return;
        if (!m_hotkeys->set(id, QKeySequence::fromString(seq, QKeySequence::PortableText)))
            failed << tr("%1 (%2)").arg(label, QKeySequence::fromString(seq, QKeySequence::PortableText)
                                                   .toString(QKeySequence::NativeText));
    };
    const QList<ActionDef> defs = m_actions->actions();
    for (const ActionDef &def : defs) {
        if (def.global)
            bind(kActionPrefix + def.id, m_actions->shortcut(def.id), def.title);
    }
    for (int i = 0; i < phrases.size(); ++i)
        bind(kPhrasePrefix + QString::number(i), phrases.at(i).hotkey, tr("Phrase “%1”").arg(phrases.at(i).text.left(24)));
    for (const Sound &s : sounds)
        bind(kSoundPrefix + s.id, s.hotkey, tr("Sound “%1”").arg(s.name));

    if (!failed.isEmpty()) {
        emit hotkeysFailed(failed);
        emit notify(tr("Another app already uses: %1").arg(failed.join(QStringLiteral(", "))), 1);
    }
}

void AppContext::applyAll()
{
    applyAudioRouting();
    applySpeechOptions();
    applySttSettings();
    applyObsSettings();
    applyOverlaySettings();
    applyMicSettings();
    applyCueSettings();
    applyTwitchSettings();
    applyHotkeys();
}

bool AppContext::debounced(const QString &id)
{
    const int window = m_settings->integer(Keys::DebounceMs);
    const qint64 now = m_clock.elapsed();
    const qint64 last = m_lastPress.value(id, -1);
    m_lastPress.insert(id, now);
    return window > 0 && last >= 0 && now - last < window;
}

void AppContext::onHotkeyPressed(const QString &id)
{
    if (debounced(id))
        return;
    if (id.startsWith(kActionPrefix)) {
        triggerAction(id.mid(kActionPrefix.size()), true);
    } else if (id.startsWith(kPhrasePrefix)) {
        const int index = id.mid(kPhrasePrefix.size()).toInt();
        const QList<Phrase> &list = m_phrases->phrases();
        if (index >= 0 && index < list.size())
            speak(list.at(index).text, list.at(index).voiceKey);
    } else if (id.startsWith(kSoundPrefix)) {
        const QString soundId = id.mid(kSoundPrefix.size());
        m_soundboard->isPlaying(soundId) ? m_soundboard->stop(soundId) : m_soundboard->play(soundId);
    }
}

void AppContext::onHotkeyReleased(const QString &id)
{
    if (id.startsWith(kActionPrefix))
        triggerAction(id.mid(kActionPrefix.size()), false);
}
