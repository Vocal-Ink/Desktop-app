#include "avatar/AvatarController.h"

#include "avatar/StreamerbotSender.h"
#include "avatar/VeadotubeClient.h"
#include "avatar/VmcSender.h"
#include "avatar/VtsClient.h"
#include "core/Settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {

using Viseme = AvatarController::Viseme;

constexpr float kBaseGain = 1.5f;    // a peak of 0.67 opens the mouth fully at 100 %
constexpr float kNoiseFloor = 0.02f; // below this the mouth stays shut
constexpr float kTalkOn = 0.08f;
constexpr float kTalkOff = 0.04f;
constexpr int kTalkHoldMs = 150;     // quiet this long before "talking" ends
constexpr int kStaleFrames = 10;     // an input not updated for ~1/3 s counts as silent
constexpr float kVisemeOpen = 0.02f; // below this the shape is Rest
constexpr int kTestSyllableMs = 170;
constexpr double kPi = 3.14159265358979323846;

// --- Letters -----------------------------------------------------------------------

enum class Kind { Rest, Vowel, Consonant, Spoken }; // Spoken: digits, other scripts

struct Letter
{
    Kind kind = Kind::Rest;
    char base = 0; // lowercase ASCII letter for Latin letters
};

char32_t codePointAt(const QString &text, int i)
{
    const QChar c = text.at(i);
    if (c.isLowSurrogate() && i > 0 && text.at(i - 1).isHighSurrogate())
        return QChar::surrogateToUcs4(text.at(i - 1), c);
    if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
        return QChar::surrogateToUcs4(c, text.at(i + 1));
    return c.unicode();
}

bool isMark(char32_t ucs)
{
    const QChar::Category cat = QChar::category(ucs);
    return cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing;
}

// The plain Latin letter under accents (e with acute -> e), or 0.
char latinBase(char32_t ucs)
{
    if (ucs < 128) {
        const char c = char(ucs);
        if (c >= 'a' && c <= 'z')
            return c;
        if (c >= 'A' && c <= 'Z')
            return char(c - 'A' + 'a');
        return 0;
    }
    switch (ucs) {
    case 0x00DF: return 's';               // sharp s
    case 0x00E6: case 0x00C6: return 'a'; // ae
    case 0x0153: case 0x0152: return 'e'; // oe
    case 0x00F8: case 0x00D8: return 'o'; // o with stroke
    case 0x0131: return 'i';               // dotless i
    case 0x00F0: case 0x00D0: case 0x0111: case 0x0110: return 'd'; // eth, d with stroke
    case 0x00FE: case 0x00DE: return 't'; // thorn
    case 0x0142: case 0x0141: return 'l'; // l with stroke
    default: break;
    }
    const QString decomposed = QChar::decomposition(ucs);
    if (!decomposed.isEmpty() && !decomposed.at(0).isSurrogate())
        return latinBase(decomposed.at(0).unicode());
    return 0;
}

bool isVowelLetter(char c)
{
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

Letter classify(const QString &text, int i)
{
    if (i < 0 || i >= text.size())
        return {};
    char32_t ucs = codePointAt(text, i);
    // A combining accent belongs to the letter before it.
    while (isMark(ucs) && i > 0) {
        --i;
        if (text.at(i).isLowSurrogate() && i > 0)
            --i;
        ucs = codePointAt(text, i);
    }
    if (QChar::isDigit(ucs))
        return {Kind::Spoken, 0};
    if (!QChar::isLetter(ucs))
        return {};
    const char base = latinBase(ucs);
    if (base == 0) {
        // Latin letters without a plain base (eng...) are consonants; other scripts just talk.
        return {QChar::script(ucs) == QChar::Script_Latin ? Kind::Consonant : Kind::Spoken, 0};
    }
    return {isVowelLetter(base) ? Kind::Vowel : Kind::Consonant, base};
}

bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

bool inWord(const QString &text, int i)
{
    if (i < 0 || i >= text.size())
        return false;
    if (isApostrophe(text.at(i)))
        return true;
    const Kind k = classify(text, i).kind;
    return k == Kind::Vowel || k == Kind::Consonant;
}

char baseAt(const QString &text, int i)
{
    const Letter l = classify(text, i);
    return l.kind == Kind::Vowel || l.kind == Kind::Consonant ? l.base : 0;
}

// Skips the second half of a surrogate pair / combining marks when stepping.
int nextIndex(const QString &text, int i)
{
    ++i;
    while (i < text.size() && (text.at(i).isLowSurrogate() || isMark(text.at(i).unicode())))
        ++i;
    return i;
}

int prevIndex(const QString &text, int i)
{
    --i;
    while (i > 0 && (text.at(i).isLowSurrogate() || isMark(text.at(i).unicode())))
        --i;
    return i;
}

Viseme vowelViseme(const QString &text, int i, char c)
{
    const char prev = baseAt(text, prevIndex(text, i));
    const char next = baseAt(text, nextIndex(text, i));
    switch (c) {
    case 'a':
        if (next == 'u' || next == 'w')
            return Viseme::O; // saw, August
        if (next == 'i' || next == 'y')
            return Viseme::E; // rain, day
        return Viseme::A;
    case 'e':
        return prev == 'e' || next == 'e' ? Viseme::I : Viseme::E; // see
    case 'i':
    case 'y':
        return prev == 'a' ? Viseme::E : Viseme::I;
    case 'o':
        return prev == 'o' || next == 'o' ? Viseme::U : Viseme::O; // moon
    case 'u':
        return prev == 'a' ? Viseme::O : Viseme::U;
    default:
        return Viseme::A;
    }
}

} // namespace

AvatarController::AvatarController(Settings *settings, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_secrets(secrets)
    , m_vts(new VtsClient(secrets, this))
    , m_vmc(new VmcSender(this))
    , m_veado(new VeadotubeClient(this))
    , m_streamerbot(new StreamerbotSender(this))
    , m_frameTimer(new QTimer(this))
{
    m_frameTimer->setInterval(kFrameMs);
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    connect(m_frameTimer, &QTimer::timeout, this, &AvatarController::tick);
    connect(m_vts, &VtsClient::accessDenied, this, [this] {
        emit notify(tr("VTube Studio did not give Vocal Ink access to your model. You can ask again on the "
                       "Avatar page."),
                    1);
    });
}

AvatarController::~AvatarController()
{
    // Don't leave a VMC avatar with its mouth open when the app quits mid-word.
    m_vmc->release();
}

QObject *AvatarController::vtsObject() const
{
    return m_vts;
}

QObject *AvatarController::vmcObject() const
{
    return m_vmc;
}

QObject *AvatarController::veadoObject() const
{
    return m_veado;
}

AvatarController::Source AvatarController::sourceFromString(const QString &source)
{
    if (source == QLatin1String("voiceAndSounds"))
        return Source::VoiceAndSounds;
    if (source == QLatin1String("everything"))
        return Source::Everything;
    return Source::Voice;
}

void AvatarController::applySettings()
{
    if (!m_settings)
        return;
    const Settings &s = *m_settings;
    const auto port = [&s](const char *key) { return quint16(std::clamp(s.integer(key), 1, 65535)); };
    const auto text = [&s](const char *key) { return s.string(key).trimmed(); };

    m_source = sourceFromString(s.string(Keys::AvatarSource));
    m_sensitivity = float(std::clamp(s.integer(Keys::AvatarSensitivity), 20, 300)) / 100.0f;
    const float smoothing = float(std::clamp(s.integer(Keys::AvatarSmoothing), 0, 100)) / 100.0f;
    m_attack = 1.0f - 0.75f * smoothing;
    m_release = 1.0f - 0.88f * smoothing;
    m_visemes = s.flag(Keys::AvatarVisemes);

    // VTube Studio
    m_vts->setPort(port(Keys::VtsPort));
    m_vts->setMouthParameters(text(Keys::VtsMouthParam), text(Keys::VtsMouthFormParam));
    m_vts->setFaceFound(s.flag(Keys::VtsFaceFound));
    m_vts->setCustomParameters(s.flag(Keys::VtsCustomParams));
    m_vtsHotkeyStart = text(Keys::VtsHotkeyStart);
    m_vtsHotkeyStop = text(Keys::VtsHotkeyStop);
    m_vtsHotkeyMicLive = text(Keys::VtsHotkeyMicLive);
    m_vtsHotkeyMicMuted = text(Keys::VtsHotkeyMicMuted);
    const QString expression = text(Keys::VtsExpression);
    if (expression != m_vtsExpression) {
        if (!m_vtsExpressionOn.isEmpty()) {
            m_vts->setExpression(m_vtsExpressionOn, false);
            m_vtsExpressionOn.clear();
        }
        m_vtsExpression = expression;
        if (m_utterance != 0 && !expression.isEmpty() && m_vts->isConnected()) {
            m_vts->setExpression(expression, true);
            m_vtsExpressionOn = expression;
        }
    }
    m_soundHotkeys.clear();
    const QJsonObject sounds = QJsonDocument::fromJson(s.string(Keys::VtsSoundHotkeys).toUtf8()).object();
    for (auto it = sounds.constBegin(); it != sounds.constEnd(); ++it) {
        const QString hotkey = it.value().toString().trimmed();
        if (!hotkey.isEmpty())
            m_soundHotkeys.insert(it.key(), hotkey);
    }
    const bool vtsOn = s.flag(Keys::VtsEnabled);
    if (!vtsOn) {
        m_vts->release();
        m_vtsExpressionOn.clear();
    }
    m_vts->setEnabled(vtsOn);

    // VMC
    m_vmc->setTarget(text(Keys::VmcHost), port(Keys::VmcPort));
    m_vmc->setBlendset(text(Keys::VmcBlendset));
    m_vmc->setGain(float(std::clamp(s.integer(Keys::VmcGain), 20, 200)) / 100.0f);
    m_vmc->setExpression(text(Keys::VmcExpression));
    m_vmc->setEnabled(s.flag(Keys::VmcEnabled));

    // veadotube
    m_veadoTalking = text(Keys::VeadoTalkingState);
    m_veadoIdle = text(Keys::VeadoIdleState);
    m_veadoMicLive = text(Keys::VeadoMicLiveState);
    m_veado->setPushToTalk(s.flag(Keys::VeadoPushToTalk));
    m_veado->setEnabled(s.flag(Keys::VeadoEnabled));

    // Streamer.bot
    m_streamerbot->setTarget(text(Keys::SbotHost), port(Keys::SbotPort));
    m_streamerbot->setEnabled(s.flag(Keys::SbotEnabled));
    m_sbotStart = text(Keys::SbotActionStart);
    m_sbotStop = text(Keys::SbotActionStop);
    m_sbotMicLive = text(Keys::SbotActionMicLive);
    m_sbotMicMuted = text(Keys::SbotActionMicMuted);
}

void AvatarController::setPluginIcon(const QByteArray &png128)
{
    m_vts->setPluginIcon(png128);
}

// --- Inputs --------------------------------------------------------------------------

namespace {
float clampLevel(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

quint64 utteranceKey(quint64 id)
{
    return id ? id : ~quint64(0); // 0 means "not speaking"
}
} // namespace

void AvatarController::onSpeechStarted(quint64 id, const QString &text)
{
    const quint64 key = utteranceKey(id);
    if (key == m_utterance) {
        m_text = text;
        return;
    }
    if (m_utterance != 0)
        speechStopReactions(); // keep start/stop pairs balanced
    m_utterance = key;
    m_text = text;
    m_progress = 0.0;
    m_lastVowel = Viseme::A;
    speechStartReactions(text);
    kick();
}

void AvatarController::onSpeechProgress(quint64 id, double fraction)
{
    if (utteranceKey(id) != m_utterance)
        return;
    m_progress = std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0) : 0.0;
}

void AvatarController::onSpeechFinished(quint64 id)
{
    if (m_utterance == 0 || utteranceKey(id) != m_utterance)
        return; // e.g. queued messages dropped by stop() that never started
    m_utterance = 0;
    m_text.clear();
    m_progress = 0.0;
    speechStopReactions();
    // The frame loop keeps going until the mouth has closed.
}

void AvatarController::onSpeechLevel(float peak)
{
    m_speechLevel = clampLevel(peak);
    m_speechAge = 0;
    m_speechLevelSeen = true;
    if (m_speechLevel > 0.0f)
        kick();
}

void AvatarController::onOutputLevel(float peak)
{
    m_outputLevel = clampLevel(peak);
    m_outputAge = 0;
    if (m_outputLevel > 0.0f)
        kick();
}

void AvatarController::onMicLevel(float level)
{
    m_micLevel = micMeterToPeak(level);
    m_micAge = 0;
    if (m_micLevel > 0.0f)
        kick();
}

void AvatarController::onMicLiveChanged(bool live)
{
    if (live == m_micLive)
        return;
    m_micLive = live;
    if (!live)
        m_micLevel = 0.0f;
    m_vts->triggerHotkey(live ? m_vtsHotkeyMicLive : m_vtsHotkeyMicMuted);
    if (m_utterance == 0) {
        const QString state = live ? m_veadoMicLive : m_veadoIdle;
        if (!state.isEmpty())
            m_veado->setState(state);
    }
    m_streamerbot->doAction(live ? m_sbotMicLive : m_sbotMicMuted);
}

void AvatarController::onSoundStarted(const QString &soundId)
{
    const QString hotkey = m_soundHotkeys.value(soundId);
    if (!hotkey.isEmpty())
        m_vts->triggerHotkey(hotkey);
}

void AvatarController::onPanic()
{
    m_testMs = 0;
    if (m_utterance != 0) {
        m_utterance = 0;
        m_text.clear();
        speechStopReactions();
    } else {
        if (!m_vtsExpressionOn.isEmpty()) {
            m_vts->setExpression(m_vtsExpressionOn, false);
            m_vtsExpressionOn.clear();
        }
        m_veado->setTalking(false);
    }
    m_speechLevel = 0.0f;
    m_outputLevel = 0.0f;
    m_micLevel = 0.0f;
    finish();
}

void AvatarController::test(int ms)
{
    m_testMs = std::clamp(ms, 100, 10000);
    m_testClock.start();
    kick();
}

void AvatarController::speechStartReactions(const QString &text)
{
    m_vts->triggerHotkey(m_vtsHotkeyStart);
    if (!m_vtsExpression.isEmpty() && m_vts->isConnected()) {
        m_vts->setExpression(m_vtsExpression, true);
        m_vtsExpressionOn = m_vtsExpression;
    }
    m_veado->setTalking(true);
    if (!m_veadoTalking.isEmpty())
        m_veado->setState(m_veadoTalking);
    m_streamerbot->doAction(m_sbotStart, {{QStringLiteral("text"), text}});
}

void AvatarController::speechStopReactions()
{
    m_vts->triggerHotkey(m_vtsHotkeyStop);
    if (!m_vtsExpressionOn.isEmpty()) {
        m_vts->setExpression(m_vtsExpressionOn, false);
        m_vtsExpressionOn.clear();
    }
    m_veado->setTalking(false);
    const QString resting = veadoRestingState();
    if (!resting.isEmpty())
        m_veado->setState(resting);
    m_streamerbot->doAction(m_sbotStop);
}

QString AvatarController::veadoRestingState() const
{
    return m_micLive && !m_veadoMicLive.isEmpty() ? m_veadoMicLive : m_veadoIdle;
}

// --- Frames --------------------------------------------------------------------------

bool AvatarController::isRunning() const
{
    return m_frameTimer->isActive();
}

bool AvatarController::isTesting() const
{
    return m_testMs > 0 && m_testClock.isValid() && m_testClock.elapsed() < m_testMs;
}

float AvatarController::micMeterToPeak(float level)
{
    if (!std::isfinite(level) || level <= 0.0f)
        return 0.0f;
    // Meter: (dBFS + 60) / 60 of the RMS. Back to linear, times a speech-like crest factor.
    const float rms = std::pow(10.0f, 3.0f * (std::min(level, 1.0f) - 1.0f));
    return std::min(1.0f, 2.0f * rms);
}

float AvatarController::formFor(Viseme viseme, float open)
{
    float form = 0.0f;
    switch (viseme) {
    case Viseme::Rest: return 0.0f;
    case Viseme::A: form = 0.15f; break;
    case Viseme::I: form = 0.6f; break;
    case Viseme::U: form = -0.5f; break;
    case Viseme::E: form = 0.4f; break;
    case Viseme::O: form = -0.3f; break;
    }
    return form * std::clamp(open * 2.5f, 0.0f, 1.0f);
}

float AvatarController::testLevel(Viseme *viseme) const
{
    if (!isTesting())
        return 0.0f;
    static const Viseme cycle[] = {Viseme::A, Viseme::E, Viseme::I, Viseme::O, Viseme::U, Viseme::A, Viseme::O, Viseme::E};
    const qint64 t = m_testClock.elapsed();
    const int n = int(t / kTestSyllableMs);
    if (viseme)
        *viseme = cycle[n % 8];
    if (n % 6 == 5)
        return 0.0f; // a short pause now and then
    const float phase = float(t % kTestSyllableMs) / float(kTestSyllableMs);
    const float peak = 0.6f + 0.4f * float((n * 7) % 5) / 4.0f;
    return peak * float(std::sin(kPi * double(phase)));
}

float AvatarController::target() const
{
    const auto fresh = [](float v, int age) { return age <= kStaleFrames ? v : 0.0f; };
    // Without the speech-only level (not connected, or a device that can't
    // tell), speech falls back to the whole output while an utterance plays.
    const float speech = m_speechLevelSeen ? fresh(m_speechLevel, m_speechAge)
                                           : (m_utterance != 0 ? fresh(m_outputLevel, m_outputAge) : 0.0f);
    float level = 0.0f;
    switch (m_source) {
    case Source::Voice:
        level = speech;
        break;
    case Source::VoiceAndSounds:
        level = m_micLive ? speech : fresh(m_outputLevel, m_outputAge);
        break;
    case Source::Everything:
        level = std::max(fresh(m_outputLevel, m_outputAge), fresh(m_micLevel, m_micAge));
        break;
    }
    float t = level < kNoiseFloor ? 0.0f : std::min(1.0f, level * kBaseGain * m_sensitivity);
    if (isTesting())
        t = std::max(t, testLevel(nullptr));
    return t;
}

void AvatarController::kick()
{
    if (m_frameTimer->isActive())
        return;
    if (target() <= 0.0f && m_utterance == 0 && !isTesting() && m_mouth <= 0.0 && !m_talking)
        return;
    m_frameTimer->start();
    tick();
}

AvatarController::Viseme AvatarController::currentViseme()
{
    if (m_mouth <= kVisemeOpen)
        return Viseme::Rest;
    if (!m_visemes)
        return Viseme::A;
    if (isTesting()) {
        Viseme v = Viseme::A;
        testLevel(&v);
        return v;
    }
    if (m_text.isEmpty())
        return m_lastVowel; // the tail of the last word, or sounds
    const int index = std::clamp(int(m_progress * double(m_text.size())), 0, int(m_text.size()) - 1);
    const Viseme v = visemeAt(m_text, index);
    if (v == Viseme::Rest)
        return m_lastVowel; // between words the mouth keeps its shape until it closes
    m_lastVowel = v;
    return v;
}

void AvatarController::setTalking(bool talking)
{
    m_quietMs = 0;
    if (talking == m_talking)
        return;
    m_talking = talking;
    m_vts->setTalking(talking);
    emit talkingChanged(talking);
}

void AvatarController::tick()
{
    const float t = target();
    const float alpha = t > m_mouth ? m_attack : m_release;
    double mouth = m_mouth + (double(t) - m_mouth) * double(alpha);
    if (std::fabs(mouth - double(t)) < 0.002)
        mouth = t;
    if (t <= 0.0f && mouth < 0.004)
        mouth = 0.0;
    m_mouth = std::clamp(mouth, 0.0, 1.0);

    // Talking, with hysteresis so short dips between syllables don't count.
    if (!m_talking) {
        if (m_mouth >= kTalkOn)
            setTalking(true);
    } else if (m_mouth < kTalkOff) {
        m_quietMs += kFrameMs;
        if (m_quietMs >= kTalkHoldMs)
            setTalking(false);
    } else {
        m_quietMs = 0;
    }

    m_viseme = currentViseme();

    m_speechAge = std::min(m_speechAge + 1, 1000);
    m_outputAge = std::min(m_outputAge + 1, 1000);
    m_micAge = std::min(m_micAge + 1, 1000);

    const bool active = m_utterance != 0 || isTesting() || t > 0.0f || m_mouth > 0.0 || m_talking;
    if (!active) {
        finish();
        return;
    }

    const float open = float(m_mouth);
    std::array<float, 5> vowels{};
    if (m_viseme != Viseme::Rest)
        vowels[size_t(int(m_viseme) - 1)] = open;
    m_vts->setMouth(open, formFor(m_viseme, open), vowels);
    m_vmc->sendMouth(open, m_viseme, m_talking || m_utterance != 0);

    if (std::fabs(m_mouth - m_emittedMouth) > 0.001 || m_viseme != m_emittedViseme) {
        m_emittedMouth = m_mouth;
        m_emittedViseme = m_viseme;
        emit frame();
    }
}

void AvatarController::finish()
{
    m_frameTimer->stop();
    m_mouth = 0.0;
    m_viseme = Viseme::Rest;
    m_lastVowel = Viseme::A;
    setTalking(false);
    if (m_vts->isInControl())
        m_vts->setMouth(0.0f, 0.0f, {});
    m_vts->release();
    m_vmc->release();
    m_emittedMouth = 0.0;
    m_emittedViseme = Viseme::Rest;
    emit frame(); // the final closed frame
}

// --- Visemes -------------------------------------------------------------------------

AvatarController::Viseme AvatarController::visemeAt(const QString &text, int index)
{
    if (index < 0 || index >= text.size())
        return Viseme::Rest;
    // An accent or the second half of a surrogate pair: look at its base character.
    while (index > 0
           && ((text.at(index).isLowSurrogate() && text.at(index - 1).isHighSurrogate())
               || isMark(codePointAt(text, index))))
        --index;
    const Letter letter = classify(text, index);
    switch (letter.kind) {
    case Kind::Rest:
        return Viseme::Rest;
    case Kind::Spoken:
        return Viseme::A;
    case Kind::Vowel:
        return vowelViseme(text, index, letter.base);
    case Kind::Consonant:
        break;
    }
    if (letter.base == 'w' || letter.base == 'q')
        return Viseme::U; // rounded lips (we, queen)
    // Other consonants take the shape of the nearest vowel in the word; the
    // mouth anticipates the next one, so look ahead first.
    for (int j = nextIndex(text, index); inWord(text, j); j = nextIndex(text, j)) {
        const Letter l = classify(text, j);
        if (l.kind == Kind::Vowel)
            return vowelViseme(text, j, l.base);
    }
    for (int j = prevIndex(text, index); j >= 0 && inWord(text, j); j = prevIndex(text, j)) {
        const Letter l = classify(text, j);
        if (l.kind == Kind::Vowel)
            return vowelViseme(text, j, l.base);
    }
    return Viseme::A; // hmm, psst
}

QString AvatarController::visemeName(Viseme viseme)
{
    switch (viseme) {
    case Viseme::A: return QStringLiteral("A");
    case Viseme::I: return QStringLiteral("I");
    case Viseme::U: return QStringLiteral("U");
    case Viseme::E: return QStringLiteral("E");
    case Viseme::O: return QStringLiteral("O");
    case Viseme::Rest: break;
    }
    return QString();
}
