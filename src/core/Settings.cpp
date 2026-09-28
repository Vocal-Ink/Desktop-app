#include "core/Settings.h"

#include "core/Paths.h"

#include <QHash>
#include <QKeySequence>

namespace {

QVariantMap defaultReplacements()
{
    return {
        {QStringLiteral("brb"), QStringLiteral("be right back")},
        {QStringLiteral("gg"), QStringLiteral("good game")},
        {QStringLiteral("idk"), QStringLiteral("I don't know")},
        {QStringLiteral("imo"), QStringLiteral("in my opinion")},
        {QStringLiteral("np"), QStringLiteral("no problem")},
        {QStringLiteral("ty"), QStringLiteral("thank you")},
        {QStringLiteral("omw"), QStringLiteral("on my way")},
        {QStringLiteral("afk"), QStringLiteral("away from keyboard")},
    };
}

const QHash<QString, QVariant> &defaults()
{
    static const QHash<QString, QVariant> d = {
        {QString::fromLatin1(Keys::OutputDevice), QByteArray()},
        {QString::fromLatin1(Keys::OutputVolume), 100},
        {QString::fromLatin1(Keys::MonitorEnabled), true},
        {QString::fromLatin1(Keys::MonitorDevice), QByteArray()},
        {QString::fromLatin1(Keys::MonitorVolume), 70},
        {QString::fromLatin1(Keys::InputDevice), QByteArray()},
        {QString::fromLatin1(Keys::Voice), QString()},
        {QString::fromLatin1(Keys::FavoriteVoices), QStringList()},
        {QString::fromLatin1(Keys::Rate), 100},
        {QString::fromLatin1(Keys::Pitch), 0},
        {QString::fromLatin1(Keys::SplitSentences), true},
        {QString::fromLatin1(Keys::AzureRegion), QStringLiteral("eastus")},
        {QString::fromLatin1(Keys::ElevenLabsModel), QStringLiteral("eleven_flash_v2_5")},
        {QString::fromLatin1(Keys::FishModel), QStringLiteral("s1")},
        {QString::fromLatin1(Keys::FishCustomVoices), QStringList()},
        {QString::fromLatin1(Keys::ElevenLabsCustomVoices), QStringList()},
        {QString::fromLatin1(Keys::OpenAiTtsModel), QStringLiteral("gpt-4o-mini-tts")},
        {QString::fromLatin1(Keys::OpenAiBaseUrl), QStringLiteral("https://api.openai.com/v1")},
        {QString::fromLatin1(Keys::OpenAiInstructions), QString()},
        {QString::fromLatin1(Keys::PiperExecutable), QString()},
        {QString::fromLatin1(Keys::SttEngine), QStringLiteral("whisper")},
        {QString::fromLatin1(Keys::SttMode), QStringLiteral("ptt")},
        {QString::fromLatin1(Keys::SttAutoSpeak), false},
        {QString::fromLatin1(Keys::SttLanguage), QStringLiteral("auto")},
        {QString::fromLatin1(Keys::SttPrompt), QString()},
        {QString::fromLatin1(Keys::SttVadSensitivity), 50},
        {QString::fromLatin1(Keys::WhisperModel), QStringLiteral("ggml-base.en-q5_1.bin")},
        {QString::fromLatin1(Keys::SttOpenAiModel), QStringLiteral("gpt-4o-mini-transcribe")},
        {QString::fromLatin1(Keys::SttOpenAiBaseUrl), QStringLiteral("https://api.openai.com/v1")},
        {QString::fromLatin1(Keys::ObsEnabled), false},
        {QString::fromLatin1(Keys::ObsHost), QStringLiteral("127.0.0.1")},
        {QString::fromLatin1(Keys::ObsPort), 4455},
        {QString::fromLatin1(Keys::ObsSubtitlesEnabled), false},
        {QString::fromLatin1(Keys::ObsSubtitlesSource), QString()},
        {QString::fromLatin1(Keys::ObsSubtitlesClearMs), 4000},
        {QString::fromLatin1(Keys::ObsCaptionsEnabled), false},
        {QString::fromLatin1(Keys::ObsIndicatorEnabled), false},
        {QString::fromLatin1(Keys::ObsIndicatorSource), QString()},
        {QString::fromLatin1(Keys::OverlayEnabled), true},
        {QString::fromLatin1(Keys::OverlayPort), 7342},
        {QString::fromLatin1(Keys::OverlayAllowLan), false},
        {QString::fromLatin1(Keys::OverlayQuery), QStringLiteral("style=subtitles")},
        {QString::fromLatin1(Keys::HotkeyPushToTalk), QStringLiteral("Ctrl+Alt+Space")},
        {QString::fromLatin1(Keys::HotkeyStop), QStringLiteral("Ctrl+Alt+S")},
        {QString::fromLatin1(Keys::HotkeyQuickType), QStringLiteral("Ctrl+Alt+T")},
        {QString::fromLatin1(Keys::HotkeyRepeat), QStringLiteral("Ctrl+Alt+R")},
        {QString::fromLatin1(Keys::Replacements), defaultReplacements()},
        {QString::fromLatin1(Keys::AutoCapitalize), true},
        {QString::fromLatin1(Keys::EmojiMode), QStringLiteral("speak")},
        {QString::fromLatin1(Keys::UrlMode), QStringLiteral("say")},
        {QString::fromLatin1(Keys::Variables), QVariantMap{{QStringLiteral("name"), QString()}}},
        {QString::fromLatin1(Keys::Predictions), 5},
        {QString::fromLatin1(Keys::Interrupt), false},
        {QString::fromLatin1(Keys::Effect), QStringLiteral("none")},
        {QString::fromLatin1(Keys::EffectIntensity), 60},
        {QString::fromLatin1(Keys::MicMode), QStringLiteral("off")},
        {QString::fromLatin1(Keys::MicDevice), QByteArray()},
        {QString::fromLatin1(Keys::MicGainDb), 0},
        {QString::fromLatin1(Keys::MicGateDb), -55},
        {QString::fromLatin1(Keys::MicDuck), true},
        {QString::fromLatin1(Keys::MicDuckDb), -18},
        {QString::fromLatin1(Keys::MicWarnOverlay), true},
        {QString::fromLatin1(Keys::MicWarnSound), true},
        {QString::fromLatin1(Keys::TwitchEnabled), false},
        {QString::fromLatin1(Keys::TwitchChannel), QString()},
        {QString::fromLatin1(Keys::TwitchVoice), QString()},
        {QString::fromLatin1(Keys::TwitchReadNames), true},
        {QString::fromLatin1(Keys::TwitchSkipCommands), true},
        {QString::fromLatin1(Keys::TwitchSkipLinks), true},
        {QString::fromLatin1(Keys::TwitchSubsOnly), false},
        {QString::fromLatin1(Keys::TwitchIgnored), QStringLiteral("nightbot, streamelements, streamlabs, moobot")},
        {QString::fromLatin1(Keys::TwitchBlocked), QString()},
        {QString::fromLatin1(Keys::Theme), QStringLiteral("midnight")},
        {QString::fromLatin1(Keys::Accent), QStringLiteral("#8c52ff")},
        {QString::fromLatin1(Keys::FontScale), 100},
        {QString::fromLatin1(Keys::FontFamily), QStringLiteral("atkinson")},
        {QString::fromLatin1(Keys::Density), QStringLiteral("comfortable")},
        {QString::fromLatin1(Keys::Corners), QStringLiteral("soft")},
        {QString::fromLatin1(Keys::SidebarLabels), true},
        {QString::fromLatin1(Keys::StageScale), 100},
        {QString::fromLatin1(Keys::ComposerSize), 17},
        {QString::fromLatin1(Keys::ShowThread), true},
        {QString::fromLatin1(Keys::ShowPhraseTray), true},
        {QString::fromLatin1(Keys::WaveStyle), QStringLiteral("ink")},
        {QString::fromLatin1(Keys::InkEffect), true},
        {QString::fromLatin1(Keys::Motion), QStringLiteral("full")},
        {QString::fromLatin1(Keys::CompactOpacity), 94},
        {QString::fromLatin1(Keys::OnboardingDone), false},
        {QString::fromLatin1(Keys::Uses), QStringList()},
        {QString::fromLatin1(Keys::FirstRunDone), false},
        {QString::fromLatin1(Keys::MinimizeToTray), true},
        {QString::fromLatin1(Keys::AlwaysOnTop), false},
        {QString::fromLatin1(Keys::ClearAfterSpeak), true},
        {QString::fromLatin1(Keys::CheckUpdates), true},
        {QString::fromLatin1(Keys::FocusRing), QStringLiteral("normal")},
        {QString::fromLatin1(Keys::LargeTargets), false},
        {QString::fromLatin1(Keys::LetterSpacing), 0},
        {QString::fromLatin1(Keys::LineSpacing), 100},
        {QString::fromLatin1(Keys::SoundCues), true},
        {QString::fromLatin1(Keys::SoundCueVolume), 45},
        {QString::fromLatin1(Keys::Announce), true},
        {QString::fromLatin1(Keys::Scanning), false},
        {QString::fromLatin1(Keys::ScanIntervalMs), 1200},
        {QString::fromLatin1(Keys::ConfirmSpeak), false},
        {QString::fromLatin1(Keys::LatchPtt), false},
        {QString::fromLatin1(Keys::EchoTyping), QStringLiteral("off")},
        {QString::fromLatin1(Keys::DebounceMs), 0},
        {QString::fromLatin1(Keys::HighlightSpoken), true},
    };
    return d;
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(Paths::isPortable()
                     ? std::make_unique<QSettings>(Paths::settingsFile(), QSettings::IniFormat)
                     : std::make_unique<QSettings>())
{
}

Settings::Settings(const QString &iniPath, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat))
{
}

QVariant Settings::defaultValue(const char *key)
{
    return defaults().value(QString::fromLatin1(key));
}

QVariant Settings::defaultValue(const QString &key)
{
    return defaults().value(key);
}

QStringList Settings::knownKeys()
{
    return defaults().keys();
}

QVariant Settings::value(const char *key) const
{
    return m_settings->value(QString::fromLatin1(key), defaultValue(key));
}

QVariant Settings::value(const char *key, const QVariant &fallback) const
{
    return m_settings->value(QString::fromLatin1(key), fallback);
}

void Settings::setValue(const char *key, const QVariant &value)
{
    const QString k = QString::fromLatin1(key);
    if (m_settings->contains(k) && m_settings->value(k) == value)
        return;
    m_settings->setValue(k, value);
    emit changed(k);
}

void Settings::remove(const char *key)
{
    const QString k = QString::fromLatin1(key);
    m_settings->remove(k);
    emit changed(k);
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const
{
    return m_settings->value(key, fallback);
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    if (m_settings->contains(key) && m_settings->value(key) == value)
        return;
    m_settings->setValue(key, value);
    emit changed(key);
}

bool Settings::contains(const QString &key) const
{
    return m_settings->contains(key);
}

void Settings::remove(const QString &key)
{
    m_settings->remove(key);
    emit changed(key);
}

QStringList Settings::childKeys(const QString &group) const
{
    m_settings->beginGroup(group);
    const QStringList keys = m_settings->childKeys();
    m_settings->endGroup();
    return keys;
}

QStringList Settings::allKeys() const
{
    // Without fallbacks: on macOS these would add system-wide keys (AppleLanguages...).
    const bool fallbacks = m_settings->fallbacksEnabled();
    m_settings->setFallbacksEnabled(false);
    const QStringList keys = m_settings->allKeys();
    m_settings->setFallbacksEnabled(fallbacks);
    return keys;
}

void Settings::sync()
{
    m_settings->sync();
}
