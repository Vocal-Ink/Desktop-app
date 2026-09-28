#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class Settings;
class SecretStore;
class VtsClient;
class VmcSender;
class VeadotubeClient;
class StreamerbotSender;

// Turns what Vocal Ink is saying into mouth movement and reactions for avatar
// software (docs/VTUBING.md): VTube Studio (plugin API), VSeeFace / Warudo /
// VNyan / VirtualMotionCapture (VMC over OSC), veadotube mini (push-to-talk and
// states over WebSocket), Streamer.bot actions, and the built-in PNGtuber
// overlay (AppContext forwards frame() to the overlay server). Any app that
// lip-syncs from a microphone also works without all this: it just listens to
// "Vocal Ink Mic".
//
// AppContext pushes the inputs below (no service pointers, so it is testable
// on its own). The controller smooths them into one mouth value (0..1) plus a
// mouth shape and fans that out to the enabled targets at <= 30 Hz while
// anything moves, then one final closed frame.
//
// Status is exposed as enums, never as sentences: QML picks the (translated)
// words.
class AvatarController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double mouth READ mouth NOTIFY frame)     // smoothed 0..1, exactly what is sent
    Q_PROPERTY(QString viseme READ viseme NOTIFY frame)  // "" | "A" | "I" | "U" | "E" | "O"
    Q_PROPERTY(bool talking READ isTalking NOTIFY talkingChanged) // with hysteresis
    Q_PROPERTY(QObject *vts READ vtsObject CONSTANT)     // VtsClient
    Q_PROPERTY(QObject *vmc READ vmcObject CONSTANT)     // VmcSender
    Q_PROPERTY(QObject *veado READ veadoObject CONSTANT) // VeadotubeClient
public:
    // Mouth shapes (VRM0 names; VRM1 is aa ih ou ee oh).
    enum class Viseme { Rest, A, I, U, E, O };
    Q_ENUM(Viseme)
    // What moves the mouth (Keys::AvatarSource: "voice" | "voiceAndSounds" | "everything").
    enum class Source { Voice, VoiceAndSounds, Everything };
    Q_ENUM(Source)

    AvatarController(Settings *settings, SecretStore *secrets, QObject *parent = nullptr);
    ~AvatarController() override;

    void applySettings(); // re-read the avatar/, vts/, vmc/, veado/ and streamerbot/ keys
    void setPluginIcon(const QByteArray &png128); // shown in VTube Studio's "Allow?" popup

    // --- Inputs (GUI thread) ------------------------------------------------
    void onSpeechStarted(quint64 id, const QString &text);
    void onSpeechProgress(quint64 id, double fraction); // 0..1 of the text heard so far
    void onSpeechFinished(quint64 id);
    void onSpeechLevel(float peak); // synthesized speech only (AudioPlayer::speechLevelChanged)
    void onOutputLevel(float peak); // everything on the main output (AudioPlayer::levelChanged)
    void onMicLevel(float level);   // real microphone passthrough (MicPassthrough::levelChanged)
    void onMicLiveChanged(bool live);
    void onSoundStarted(const QString &soundId); // runs the sound's VTS hotkey (Keys::VtsSoundHotkeys)
    void onPanic(); // mouth closed, expressions off, everything released now

    // Moves the mouth like a short sentence on every enabled target.
    Q_INVOKABLE void test(int ms = 1500);

    double mouth() const { return m_mouth; }
    QString viseme() const { return visemeName(m_viseme); }
    Viseme visemeValue() const { return m_viseme; }
    bool isTalking() const { return m_talking; }
    VtsClient *vts() const { return m_vts; }
    VmcSender *vmc() const { return m_vmc; }
    VeadotubeClient *veado() const { return m_veado; }
    StreamerbotSender *streamerbot() const { return m_streamerbot; }
    QObject *vtsObject() const;
    QObject *vmcObject() const;
    QObject *veadoObject() const;

    // The mouth shape for the letter at `index` of `text` (vowels and a few
    // consonant groups; Rest for spaces and punctuation). Pure; for tests.
    static Viseme visemeAt(const QString &text, int index);
    static QString visemeName(Viseme viseme); // "", "A", "I", "U", "E", "O"

signals:
    void frame(); // <= 30 Hz while the mouth moves, then one final closed frame
    void talkingChanged(bool talking);
    void notify(const QString &message, int level); // 0 info, 1 warning, 2 error

private:
    Settings *m_settings;
    SecretStore *m_secrets;
    VtsClient *m_vts = nullptr;
    VmcSender *m_vmc = nullptr;
    VeadotubeClient *m_veado = nullptr;
    StreamerbotSender *m_streamerbot = nullptr;
    double m_mouth = 0.0;
    Viseme m_viseme = Viseme::Rest;
    bool m_talking = false;
};
