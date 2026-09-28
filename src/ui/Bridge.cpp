#include "ui/Bridge.h"

#include "Version.h"
#include "app/AppContext.h"
#include "avatar/AvatarController.h"
#include "audio/AudioPlayer.h"
#include "audio/MicPassthrough.h"
#include "audio/RoutingCheck.h"
#include "audio/VoiceEffects.h"
#include "core/ActionRegistry.h"
#include "core/HistoryModel.h"
#include "core/Paths.h"
#include "core/PhraseStore.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "core/SettingsTransfer.h"
#include "core/SpeechQueue.h"
#include "core/UpdateChecker.h"
#include "core/VoicePresets.h"
#include "core/WordPredictor.h"
#include "obs/ObsIntegration.h"
#include "obs/OverlayServer.h"
#include "obs/TwitchChat.h"
#include "platform/GlobalHotkeys.h"
#include "platform/VirtualAudio.h"
#include "platform/VirtualDriver.h"
#include "stt/SttController.h"
#include "tts/TtsRegistry.h"
#include "ui/Models.h"
#include "ui/PrefsMap.h"
#include "ui/VoicePreview.h"

#include <QAccessible>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMediaDevices>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QtMath>

namespace {
// Reading speed used to pace the ink fill when the engine can't tell us
// how much has been heard yet.
constexpr double kWordsPerSecond = SpeechQueue::kWordsPerSecond;

bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_unknown:
    case 0:
        return true;
    default:
        return false;
    }
}
} // namespace

Bridge::Bridge(AppContext *context, QObject *parent)
    : QObject(parent)
    , m_ctx(context)
    , m_prefs(new PrefsMap(context->settings(), this))
    , m_voices(new VoiceModel(context->tts(), context->settings(), this))
    , m_phrases(new PhraseModel(context->phrases(), this))
    , m_sounds(new SoundModel(context->soundboard(), this))
    , m_keybinds(new KeybindModel(context->actions(), this))
    , m_outputs(new DeviceModel(true, this))
    , m_inputs(new DeviceModel(false, this))
    , m_whisper(new DownloadModel(DownloadModel::Kind::Whisper, context->models(), context->settings(), this))
    , m_piper(new DownloadModel(DownloadModel::Kind::Piper, context->models(), context->settings(), this))
    , m_presetModel(new PresetModel(context->presets(), this))
    , m_preview(new VoicePreview(context, this))
    , m_echo(new VoicePreview(context, this))
    , m_routing(new RoutingCheck(this))
    , m_progressTimer(new QTimer(this))
{
    SpeechQueue *speech = m_ctx->speech();

    // --- Speaking & the ink fill ---
    connect(speech, &SpeechQueue::speakingChanged, this, [this](bool speaking) {
        m_speaking = speaking;
        emit speakingChanged();
    });
    connect(speech, &SpeechQueue::queueChanged, this, [this](int queued) {
        m_queued = queued;
        emit queuedChanged();
    });
    connect(speech, &SpeechQueue::started, this, [this](quint64 id, const QString &text, const Voice &) {
        setCurrentLine(id, text);
    });
    connect(speech, &SpeechQueue::finished, this, [this](quint64 id, const QString &text, bool completed) {
        if (id != m_currentId)
            return;
        m_progressTimer->stop();
        if (completed) {
            setLineProgress(1.0);
            emit spoken(text);
        }
    });
    connect(speech, &SpeechQueue::failed, this, [this](quint64 id, const QString &, const QString &) {
        if (id == m_currentId)
            m_progressTimer->stop();
    });
    connect(speech, &SpeechQueue::progress, this,
            [this](quint64 id, qint64 playedMs, qint64 totalMs, bool totalKnown) {
                if (id != m_currentId || totalMs <= 0)
                    return;
                m_haveRealProgress = true;
                setLineProgress(SpeechQueue::progressFraction(m_currentLine, playedMs, totalMs, totalKnown,
                                                              m_ctx->speech()->options().rate));
            });
    m_progressTimer->setInterval(33);
    connect(m_progressTimer, &QTimer::timeout, this, [this] {
        if (m_haveRealProgress)
            return;
        const int words = qMax(1, int(m_currentLine.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()));
        const double seconds = words / kWordsPerSecond / qMax(0.5, m_ctx->speech()->options().rate);
        setLineProgress(qMin(0.96, m_lineClock.elapsed() / 1000.0 / seconds));
    });
    connect(m_ctx->player(), &AudioPlayer::levelChanged, this, [this](float peak) {
        m_outputLevel = peak;
        emit outputLevelChanged();
    });
    connect(m_ctx, &AppContext::captionsPausedChanged, this, &Bridge::captionsPausedChanged);

    // --- Voice ---
    connect(m_ctx, &AppContext::currentVoiceChanged, this, &Bridge::voiceChanged);
    connect(m_preview, &VoicePreview::playingChanged, this, [this](bool playing) {
        m_previewing = playing;
        if (!playing)
            m_previewKey.clear();
        emit previewingChanged();
    });
    connect(m_preview, &VoicePreview::failed, this, [this](const QString &msg) { emit notify(msg, 1); });

    // --- Listening ---
    SttController *stt = m_ctx->stt();
    connect(stt, &SttController::listeningChanged, this, &Bridge::listeningChanged);
    connect(stt, &SttController::busyChanged, this, [this](bool busy) {
        m_transcribing = busy;
        emit transcribingChanged();
    });
    connect(stt, &SttController::speechActiveChanged, this, [this](bool active) {
        m_voiceActive = active;
        emit voiceActiveChanged();
    });
    connect(stt, &SttController::levelChanged, this, [this](float level) {
        m_micLevel = level;
        emit micLevelChanged();
    });
    auto *sttPoll = new QTimer(this);
    sttPoll->setInterval(1500);
    connect(sttPoll, &QTimer::timeout, this, &Bridge::sttChanged);
    sttPoll->start();

    // --- Real mic ---
    MicPassthrough *mic = m_ctx->mic();
    connect(mic, &MicPassthrough::liveChanged, this, &Bridge::micLiveChanged);
    connect(mic, &MicPassthrough::levelChanged, this, [this](float level) {
        m_micLiveLevel = level;
        emit micLiveLevelChanged();
    });
    connect(m_ctx, &AppContext::micLiveChanged, this, &Bridge::micLiveWarning);

    // --- Routing ---
    connect(m_ctx->settings(), &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String(Keys::OutputDevice))
            emit routeChanged();
        else if (key.startsWith(QLatin1String("overlay/")))
            QTimer::singleShot(300, this, &Bridge::overlayChanged);
    });
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        m_outputs->refresh();
        emit routeChanged();
    });
    connect(devices, &QMediaDevices::audioInputsChanged, m_inputs, &DeviceModel::refresh);
    connect(m_ctx->virtualDriver(), &VirtualDriver::stateChanged, this, [this] {
        m_outputs->refresh();
        m_inputs->refresh();
        emit routeChanged();
    });
    connect(m_routing, &RoutingCheck::progress, this, [this](float level) { emit routingProgress(level); });
    connect(m_routing, &RoutingCheck::finished, this, [this](bool heard, const QString &detail) {
        emit routingCheckRunningChanged();
        emit routingFinished(heard, detail);
    });

    // --- Stream ---
    connect(m_ctx->obs(), &ObsIntegration::statusChanged, this, &Bridge::obsChanged);
    connect(m_ctx->overlay(), &OverlayServer::clientCountChanged, this, &Bridge::overlayChanged);
    connect(m_ctx->twitch(), &TwitchChat::statusChanged, this, [this](bool, const QString &status) {
        m_twitchStatus = status;
        emit twitchChanged();
    });

    // --- App ---
    connect(m_ctx, &AppContext::notify, this, &Bridge::notify);
    connect(m_ctx, &AppContext::transcriptReady, this, &Bridge::transcriptReady);
    connect(m_ctx, &AppContext::uiActionRequested, this, &Bridge::uiAction);
    connect(m_ctx, &AppContext::hotkeysFailed, this, &Bridge::hotkeysFailed);
    connect(m_ctx->secrets(), &SecretStore::changed, this, [this] {
        ++m_secretsRevision;
        emit secretsChanged();
    });
    connect(m_ctx->secrets(), &SecretStore::loaded, this, [this] {
        ++m_secretsRevision;
        emit secretsChanged();
    });
    connect(m_ctx->predictor(), &WordPredictor::learnedChanged, this, &Bridge::learnedWordsChanged);
    connect(m_ctx->actions(), &ActionRegistry::shortcutChanged, this, &Bridge::shortcutsChanged);
    connect(m_ctx->actions(), &ActionRegistry::shortcutsReset, this, &Bridge::shortcutsChanged);
    UpdateChecker *updates = m_ctx->updates();
    connect(updates, &UpdateChecker::updateAvailable, this,
            [this](const QString &version, const QUrl &page, const QString &notes) {
                m_updateVersion = version;
                m_updateUrl = page.toString();
                m_updateNotes = notes;
                emit updateChanged();
            });
}

Bridge::~Bridge()
{
    m_preview->stop();
    m_echo->stop();
}

QObject *Bridge::prefs() const { return m_prefs; }
QObject *Bridge::voices() const { return m_voices; }
QObject *Bridge::phrases() const { return m_phrases; }
QObject *Bridge::sounds() const { return m_sounds; }
QObject *Bridge::keybinds() const { return m_keybinds; }
QObject *Bridge::outputs() const { return m_outputs; }
QObject *Bridge::inputs() const { return m_inputs; }
QObject *Bridge::whisperModels() const { return m_whisper; }
QObject *Bridge::piperVoices() const { return m_piper; }
QObject *Bridge::presets() const { return m_presetModel; }
QObject *Bridge::history() const { return m_ctx->history(); }
QObject *Bridge::virtualMic() const { return m_ctx->virtualDriver(); }
QObject *Bridge::avatar() const { return m_ctx->avatar(); }

bool Bridge::captionsPaused() const
{
    return m_ctx->captionsPaused();
}

void Bridge::setCurrentLine(quint64 id, const QString &text)
{
    m_currentId = id;
    m_currentLine = text;
    m_haveRealProgress = false;
    m_lineProgress = 0.0;
    m_lineClock.restart();
    m_progressTimer->start();
    emit currentLineChanged();
    emit lineProgressChanged();
}

void Bridge::setLineProgress(double progress)
{
    if (qFuzzyCompare(progress + 1.0, m_lineProgress + 1.0))
        return;
    m_lineProgress = progress;
    emit lineProgressChanged();
}

void Bridge::startDemo()
{
    HistoryModel *h = m_ctx->history();
    const QString voice = voiceName();
    const QStringList lines = {
        tr("Hey! Sorry, I don't talk out loud, so I type."),
        tr("Give me one second to catch up."),
        tr("Okay, that was a great point about the release plan."),
        tr("Can we push the demo to Thursday so I have time to rehearse it with everyone?"),
    };
    for (int i = 0; i < lines.size(); ++i) {
        h->add(quint64(9000 + i), lines.at(i), voice);
        h->setStatus(quint64(9000 + i), i + 1 < lines.size() ? HistoryModel::Status::Done : HistoryModel::Status::Speaking);
    }
    m_currentId = 9000 + quint64(lines.size()) - 1;
    m_currentLine = lines.last();
    m_lineProgress = 0.52;
    m_speaking = true;
    emit currentLineChanged();
    emit lineProgressChanged();
    emit speakingChanged();
    // A voice-like level so the ink stroke has something to draw.
    auto *t = new QTimer(this);
    t->setInterval(33);
    connect(t, &QTimer::timeout, this, [this] {
        static int n = 0;
        ++n;
        const double syllable = std::abs(std::sin(n * 0.21)) * (0.55 + 0.45 * std::sin(n * 0.037));
        m_outputLevel = qBound(0.0, syllable * 0.9, 1.0);
        emit outputLevelChanged();
    });
    t->start();
}

// --- Voice ----------------------------------------------------------------------

Voice Bridge::voice() const
{
    return m_ctx->currentVoice();
}

QString Bridge::voiceKey() const
{
    return voice().key();
}

QString Bridge::voiceName() const
{
    const Voice v = voice();
    return v.isValid() ? v.name : QString();
}

QString Bridge::voiceProvider() const
{
    return voice().engineId;
}

QString Bridge::voiceProviderName() const
{
    const TtsEngine *e = m_ctx->tts()->engine(voice().engineId);
    return e ? e->displayName() : QString();
}

QString Bridge::voiceLanguage() const
{
    return VoiceModel::languageLabel(voice().language);
}

void Bridge::setVoice(const QString &key)
{
    const Voice v = m_ctx->tts()->resolve(key);
    if (v.isValid())
        m_ctx->setCurrentVoice(v);
}

QVariantMap Bridge::voiceInfo(const QString &key) const
{
    const Voice v = m_ctx->tts()->resolve(key);
    const TtsEngine *e = m_ctx->tts()->engine(v.engineId);
    QString initials;
    const QStringList words = v.name.split(QRegularExpression(QStringLiteral("[\\s_\\-()]+")), Qt::SkipEmptyParts);
    for (const QString &w : words) {
        if (initials.size() < 2 && w.at(0).isLetterOrNumber())
            initials += w.at(0).toUpper();
    }
    return {{QStringLiteral("key"), v.key()},
            {QStringLiteral("name"), v.name},
            {QStringLiteral("provider"), v.engineId},
            {QStringLiteral("providerName"), e ? e->displayName() : v.engineId},
            {QStringLiteral("language"), VoiceModel::languageLabel(v.language)},
            {QStringLiteral("local"), e ? e->isLocal() : false},
            {QStringLiteral("initials"), initials},
            {QStringLiteral("valid"), v.isValid()}};
}

void Bridge::previewVoice(const QString &key, const QString &text)
{
    const Voice v = m_ctx->tts()->resolve(key);
    if (!v.isValid())
        return;
    m_previewKey = key;
    emit previewingChanged();
    const QString line = text.isEmpty() ? tr("Hi, I'm %1. This is how I'd sound for you.").arg(v.name) : text;
    m_preview->play(v, line, text.isEmpty());
}

void Bridge::stopPreview()
{
    m_preview->stop();
}

void Bridge::applyPreset(const QString &presetId)
{
    m_ctx->applyPreset(presetId);
}

QString Bridge::saveCurrentAsPreset(const QString &name)
{
    Settings *s = m_ctx->settings();
    VoicePreset p;
    p.name = name.trimmed().isEmpty() ? voiceName() : name.trimmed();
    p.voiceKey = voiceKey();
    p.rate = s->integer(Keys::Rate);
    p.pitch = s->integer(Keys::Pitch);
    p.effect = s->string(Keys::Effect);
    p.effectIntensity = s->integer(Keys::EffectIntensity);
    return m_ctx->presets()->add(p);
}

void Bridge::refreshVoices()
{
    m_ctx->tts()->refreshAll();
}

void Bridge::searchProvider(const QString &providerId, const QString &query)
{
    if (TtsEngine *e = m_ctx->tts()->engine(providerId); e && e->supportsRemoteSearch())
        e->searchVoices(query);
}

void Bridge::addCustomVoice(const QString &providerId, const QString &voiceId, const QString &name)
{
    if (TtsEngine *e = m_ctx->tts()->engine(providerId); e && e->supportsCustomVoiceIds())
        e->addCustomVoice(voiceId.trimmed(), name.trimmed());
}

void Bridge::testOutput()
{
    m_ctx->speak(tr("Testing, one, two, three. This is how people will hear me."));
}

// --- Speaking -------------------------------------------------------------------

quint64 Bridge::speak(const QString &text, const QString &voiceKey)
{
    return m_ctx->speak(text, voiceKey);
}

QString Bridge::prepare(const QString &text) const
{
    return m_ctx->prepareText(text);
}

void Bridge::stop()
{
    m_ctx->stopSpeaking();
}

void Bridge::skip()
{
    m_ctx->skipCurrent();
}

void Bridge::repeatLast()
{
    m_ctx->repeatLast();
}

void Bridge::panic()
{
    m_ctx->panic();
}

void Bridge::clearHistory()
{
    m_ctx->history()->clear();
}

QStringList Bridge::recentTexts() const
{
    return m_ctx->history()->texts();
}

QStringList Bridge::suggest(const QString &textBeforeCursor) const
{
    const int count = m_ctx->settings()->integer(Keys::Predictions);
    if (count <= 0)
        return {};
    return m_ctx->predictor()->suggest(textBeforeCursor, count);
}

QString Bridge::applySuggestion(const QString &textBeforeCursor, const QString &word) const
{
    return WordPredictor::applySuggestion(textBeforeCursor, word);
}

void Bridge::forgetLearnedWords()
{
    m_ctx->predictor()->clearLearned();
}

void Bridge::echo(const QString &text)
{
    const Voice v = voice();
    if (v.isValid() && !text.trimmed().isEmpty())
        m_echo->play(v, text, false);
}

int Bridge::learnedWords() const
{
    return m_ctx->predictor()->learnedWordCount();
}

// --- Actions & shortcuts ----------------------------------------------------------

void Bridge::trigger(const QString &actionId)
{
    if (actionId.startsWith(QLatin1String("phrase:"))) {
        const int index = actionId.mid(7).toInt();
        const QList<Phrase> &list = m_ctx->phrases()->phrases();
        if (index >= 0 && index < list.size())
            m_ctx->speak(list.at(index).text, list.at(index).voiceKey);
        return;
    }
    m_ctx->triggerAction(actionId, true);
}

void Bridge::triggerHold(const QString &actionId, bool pressed)
{
    m_ctx->triggerAction(actionId, pressed);
}

void Bridge::suspendHotkeys(bool suspended)
{
    m_ctx->setHotkeysSuspended(suspended);
}

QString Bridge::sequenceFromKey(int key, int modifiers) const
{
    if (isModifierKey(key))
        return {};
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers)
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    if (key == Qt::Key_Backtab)
        key = Qt::Key_Tab;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

QString Bridge::nativeShortcut(const QString &portable) const
{
    return QKeySequence::fromString(portable, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QStringList Bridge::shortcutParts(const QString &portable) const
{
    const QKeySequence seq = QKeySequence::fromString(portable, QKeySequence::PortableText);
    if (seq.isEmpty())
        return {};
    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    QStringList parts;
#ifdef Q_OS_MACOS
    // On macOS Qt maps Ctrl to Command and Meta to Control.
    if (mods & Qt::MetaModifier)
        parts << QStringLiteral("⌃");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("⌥");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("⇧");
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("⌘");
#else
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("Alt");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("Shift");
    if (mods & Qt::MetaModifier)
        parts << (platform() == QLatin1String("windows") ? QStringLiteral("Win") : QStringLiteral("Super"));
#endif
    QString key = QKeySequence(combo.key()).toString(QKeySequence::NativeText);
    if (combo.key() == Qt::Key_Space)
        key = tr("Space");
    else if (combo.key() == Qt::Key_Return || combo.key() == Qt::Key_Enter)
#ifdef Q_OS_MACOS
        key = QStringLiteral("↩");
#else
        key = tr("Enter");
#endif
    else if (combo.key() == Qt::Key_Escape)
        key = QStringLiteral("Esc");
    parts << key;
    return parts;
}

QVariantMap Bridge::shortcuts() const
{
    QVariantMap out;
    const QList<ActionDef> defs = m_ctx->actions()->actions();
    for (const ActionDef &d : defs)
        out.insert(d.id, m_ctx->actions()->shortcut(d.id));
    return out;
}

QString Bridge::shortcutFor(const QString &actionId) const
{
    return nativeShortcut(m_ctx->actions()->shortcut(actionId));
}

QVariantList Bridge::commands() const
{
    QVariantList out;
    const QList<ActionDef> defs = m_ctx->actions()->actions();
    for (const ActionDef &d : defs) {
        out << QVariantMap{{QStringLiteral("id"), d.id},
                           {QStringLiteral("title"), d.title},
                           {QStringLiteral("detail"), d.description},
                           {QStringLiteral("category"), d.category},
                           {QStringLiteral("shortcut"), m_ctx->actions()->shortcut(d.id)},
                           {QStringLiteral("hold"), d.hold}};
    }
    const QList<Phrase> &phrases = m_ctx->phrases()->phrases();
    for (int i = 0; i < phrases.size(); ++i) {
        out << QVariantMap{{QStringLiteral("id"), QStringLiteral("phrase:%1").arg(i)},
                           {QStringLiteral("title"), tr("Say “%1”").arg(phrases.at(i).text)},
                           {QStringLiteral("detail"), phrases.at(i).category},
                           {QStringLiteral("category"), tr("Phrases")},
                           {QStringLiteral("shortcut"), phrases.at(i).hotkey},
                           {QStringLiteral("hold"), false}};
    }
    return out;
}

// --- Listening ------------------------------------------------------------------

bool Bridge::listening() const
{
    return m_ctx->stt()->isListening();
}

bool Bridge::sttReady() const
{
    const SttEngine *e = m_ctx->sttEngine();
    return e && e->isReady();
}

QString Bridge::sttStatus() const
{
    const SttEngine *e = m_ctx->sttEngine();
    if (!e)
        return tr("Speech input is off.");
    if (e->isReady())
        return tr("%1 is ready.").arg(e->displayName());
    const QString reason = e->notReadyReason();
    return reason.isEmpty() ? tr("%1 is loading…").arg(e->displayName()) : reason;
}

void Bridge::startListening()
{
    m_ctx->stt()->startListening();
}

void Bridge::stopListening()
{
    m_ctx->stt()->stopListening();
}

void Bridge::toggleListening()
{
    m_ctx->stt()->toggleListening();
}

void Bridge::cancelListening()
{
    m_ctx->stt()->cancel();
}

// --- Real mic -------------------------------------------------------------------

bool Bridge::micLive() const
{
    return m_ctx->mic()->isLive();
}

void Bridge::setMicLive(bool live)
{
    m_ctx->mic()->setLive(live);
}

// --- Routing --------------------------------------------------------------------

QString Bridge::routeName() const
{
    const QByteArray id = m_ctx->settings()->value(Keys::OutputDevice).toByteArray();
    if (id.isEmpty())
        return QMediaDevices::defaultAudioOutput().description();
    return m_outputs->nameOf(QString::fromLatin1(id.toHex()));
}

QString Bridge::routeState() const
{
    const QByteArray id = m_ctx->settings()->value(Keys::OutputDevice).toByteArray();
    if (!id.isEmpty() && m_outputs->indexOfId(QString::fromLatin1(id.toHex())) < 0)
        return QStringLiteral("missing");
    return VirtualAudio::looksLikeVirtualCable(routeName()) ? QStringLiteral("virtual") : QStringLiteral("speakers");
}

bool Bridge::routingCheckRunning() const
{
    return m_routing->isRunning();
}

void Bridge::startRoutingCheck()
{
    const QByteArray out = m_ctx->settings()->value(Keys::OutputDevice).toByteArray();
    m_routing->start(out, RoutingCheck::pairedInputFor(out));
    emit routingCheckRunningChanged();
}

void Bridge::cancelRoutingCheck()
{
    m_routing->cancel();
    emit routingCheckRunningChanged();
}

void Bridge::useDevice(const QString &which, const QString &hexId)
{
    const QByteArray id = QByteArray::fromHex(hexId.toLatin1());
    Settings *s = m_ctx->settings();
    if (which == QLatin1String("output"))
        s->setValue(Keys::OutputDevice, id);
    else if (which == QLatin1String("monitor"))
        s->setValue(Keys::MonitorDevice, id);
    else if (which == QLatin1String("input"))
        s->setValue(Keys::InputDevice, id);
    else if (which == QLatin1String("mic"))
        s->setValue(Keys::MicDevice, id);
}

QString Bridge::suggestedOutput() const
{
    QByteArray id = m_ctx->virtualDriver()->outputDeviceId();
    if (id.isEmpty())
        id = VirtualAudio::detectVirtualCableOutput();
    return QString::fromLatin1(id.toHex());
}

// --- Stream ---------------------------------------------------------------------

int Bridge::obsStatus() const
{
    return int(m_ctx->obs()->status());
}

QString Bridge::obsStatusText() const
{
    return m_ctx->obs()->statusText();
}

bool Bridge::overlayRunning() const
{
    return m_ctx->overlay()->isRunning();
}

QString Bridge::overlayUrl() const
{
    return m_ctx->overlay()->isRunning() ? m_ctx->overlayUrl() : QString();
}

int Bridge::overlayClients() const
{
    return m_ctx->overlay()->clientCount();
}

bool Bridge::twitchConnected() const
{
    return m_ctx->twitch()->isConnected();
}

void Bridge::obsReconnect()
{
    m_ctx->obs()->reconnect();
}

void Bridge::obsTest()
{
    m_ctx->obs()->testSubtitle([this](bool ok, const QString &message) { emit obsResult(ok, message); });
}

void Bridge::obsAddOverlay()
{
    if (!m_ctx->overlay()->isRunning()) {
        emit obsResult(false, tr("Turn on the caption overlay first."));
        return;
    }
    m_ctx->obs()->addBrowserOverlay(QStringLiteral("Vocal Ink captions"), QUrl(m_ctx->overlayUrl()),
                                    [this](bool ok, const QString &message) { emit obsResult(ok, message); });
}

void Bridge::obsFetchSources(const QString &kind)
{
    auto done = [this, kind](const QStringList &names, const QString &error) { emit obsSources(kind, names, error); };
    if (kind == QLatin1String("text"))
        m_ctx->obs()->listTextSources(done);
    else
        m_ctx->obs()->listAllSources(done);
}

void Bridge::obsCreateTextSource(const QString &name)
{
    m_ctx->obs()->createTextSource(name, [this, name](bool ok, const QString &message) {
        if (ok)
            m_ctx->settings()->setValue(Keys::ObsSubtitlesSource, name);
        emit obsResult(ok, message);
    });
}

QString Bridge::overlayUrlFor(const QString &query) const
{
    return m_ctx->overlay()->overlayUrl(query).toString();
}

void Bridge::clearCaptions()
{
    m_ctx->overlay()->clearCaptions();
}

void Bridge::setCaptionsPaused(bool paused)
{
    m_ctx->setCaptionsPaused(paused);
}

// --- Keys, files, misc -------------------------------------------------------------

bool Bridge::hasSecret(const QString &name) const
{
    return m_ctx->secrets()->has(name);
}

QString Bridge::secretHint(const QString &name) const
{
    const QString v = m_ctx->secrets()->get(name);
    if (v.isEmpty())
        return {};
    return QStringLiteral("••••") + v.right(4);
}

void Bridge::setSecret(const QString &name, const QString &value)
{
    m_ctx->secrets()->set(name, value.trimmed());
}

QString Bridge::exportSettings(const QUrl &file)
{
    QString error;
    if (!SettingsTransfer::exportTo(file.toLocalFile(), m_ctx->settings(), &error))
        return error.isEmpty() ? tr("Could not save the file.") : error;
    return {};
}

QString Bridge::importSettings(const QUrl &file)
{
    QString error;
    if (!SettingsTransfer::importFrom(file.toLocalFile(), m_ctx->settings(), &error))
        return error.isEmpty() ? tr("That file isn't a Vocal Ink settings backup.") : error;
    m_ctx->applyAll();
    return {};
}

void Bridge::checkForUpdates()
{
    UpdateChecker *updates = m_ctx->updates();
    auto *once = new QObject(this);
    connect(updates, &UpdateChecker::upToDate, once, [this, once] {
        emit notify(tr("You have the latest version (%1).").arg(version()), 0);
        once->deleteLater();
    });
    connect(updates, &UpdateChecker::failed, once, [this, once](const QString &msg) {
        emit notify(tr("Couldn't check for updates: %1").arg(msg), 1);
        once->deleteLater();
    });
    connect(updates, &UpdateChecker::updateAvailable, once, [once] { once->deleteLater(); });
    updates->check();
}

void Bridge::copy(const QString &text)
{
    if (QClipboard *c = QGuiApplication::clipboard())
        c->setText(text);
}

QString Bridge::clipboardText() const
{
    const QClipboard *c = QGuiApplication::clipboard();
    return c ? c->text() : QString();
}

void Bridge::openUrl(const QString &url)
{
    QDesktopServices::openUrl(QUrl(url));
}

void Bridge::openDataFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataDir()));
}

QString Bridge::dataFolder() const
{
    return Paths::dataDir();
}

void Bridge::announce(const QString &text, bool assertive)
{
    if (!m_ctx->settings()->flag(Keys::Announce) || !m_window || text.isEmpty())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent event(m_window, text);
    event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive
                                  : QAccessible::AnnouncementPoliteness::Polite);
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(assertive)
#endif
}

void Bridge::notifyUser(const QString &message, int level)
{
    emit notify(message, level);
}

void Bridge::quit()
{
    QCoreApplication::quit();
}

QString Bridge::localPath(const QUrl &url) const
{
    return url.toLocalFile();
}

QUrl Bridge::fileUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}

QString Bridge::platform() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString Bridge::version() const
{
    return QStringLiteral(VOCALINK_VERSION);
}

bool Bridge::hotkeysSupported() const
{
    return GlobalHotkeys::isSupported();
}

QString Bridge::hotkeysUnsupportedReason() const
{
    return GlobalHotkeys::unsupportedReason();
}

QVariantList Bridge::effects() const
{
    QVariantList out;
    const QList<VoiceEffects::Effect> all = VoiceEffects::all();
    for (VoiceEffects::Effect e : all) {
        out << QVariantMap{{QStringLiteral("id"), VoiceEffects::id(e)},
                           {QStringLiteral("name"), VoiceEffects::displayName(e)},
                           {QStringLiteral("description"), VoiceEffects::description(e)}};
    }
    return out;
}
