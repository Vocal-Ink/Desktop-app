#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Vocal Ink's own virtual microphone, shipped with the app so people don't
// have to find and install a third-party cable.
//
//   Windows  VocalInkAudio.sys (a render + capture loopback pair) installed
//            with nefconw. Only offered when the build carries a signed
//            driver package next to the executable; otherwise the state is
//            Unavailable and the UI points to VB-CABLE.
//   macOS    VocalInkVirtualMic.driver (a Core Audio HAL plug-in) copied to
//            /Library/Audio/Plug-Ins/HAL with an administrator prompt.
//   Linux    A persistent PipeWire/PulseAudio null sink + virtual source, no
//            admin rights needed.
//
// In every case the app plays into outputDeviceName() and other apps pick
// inputDeviceName() as their microphone.
class VirtualDriver : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool needsAdmin READ needsAdmin CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString outputDeviceName READ outputDeviceName CONSTANT)
    Q_PROPERTY(QString inputDeviceName READ inputDeviceName CONSTANT)
public:
    enum class State {
        Unsupported,   // no bundled driver for this OS
        Unavailable,   // this build has no (signed) driver package
        NotInstalled,
        Installed,     // present and its devices are visible
        RestartNeeded, // installed, but the OS must restart before it appears
        Failed         // last install/uninstall attempt failed (see statusText)
    };
    Q_ENUM(State)

    explicit VirtualDriver(QObject *parent = nullptr);
    ~VirtualDriver() override;

    State state() const { return m_state; }
    QString statusText() const { return m_statusText; }
    bool busy() const { return m_busy; }
    bool needsAdmin() const;
    QString displayName() const;      // "Vocal Ink Virtual Mic"
    QString outputDeviceName() const; // what Vocal Ink plays into
    QString inputDeviceName() const;  // what Discord/OBS/games pick as the mic

    // Id of the output device Vocal Ink should play into, or empty.
    QByteArray outputDeviceId() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void install();
    Q_INVOKABLE void uninstall();

signals:
    void stateChanged();
    // Emitted once per install()/uninstall(). `message` is user-facing.
    void finished(bool ok, const QString &message);

private:
    void setState(State state, const QString &text);

    State m_state = State::Unsupported;
    QString m_statusText;
    bool m_busy = false;
};
