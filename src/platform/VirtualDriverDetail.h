#pragma once

#include "platform/VirtualDriver.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Pure helpers behind VirtualDriver. Everything here is plain text/logic with no
// system access, so it builds and is unit-tested on every OS (tests/test_virtualdriver.cpp).
namespace VirtualDriverDetail {

// --- Names shared with the drivers ----------------------------------------------
// Keep these in sync with driver/macos/VocalInkVirtualMic.cpp and the Windows INF.
inline constexpr auto DisplayName = "Vocal Ink Virtual Mic";
inline constexpr auto VoiceName = "Vocal Ink Voice"; // the output Vocal Ink plays into
inline constexpr auto MicName = "Vocal Ink Mic";     // what other apps pick as the microphone

inline constexpr auto MacDriverBundleName = "VocalInkVirtualMic.driver";
inline constexpr auto MacHalPluginDir = "/Library/Audio/Plug-Ins/HAL";
inline constexpr auto MacDeviceName = "Vocal Ink Virtual Mic"; // HAL device name (one device, both directions)

inline constexpr auto WindowsHardwareId = "ROOT\\VocalInkAudio";
inline constexpr auto WindowsMediaClassGuid = "4d36e96c-e325-11ce-bfc1-08002be10318";
inline constexpr auto WindowsInfName = "VocalInkAudio.inf";
inline constexpr int WindowsRebootRequired = 3010; // ERROR_SUCCESS_REBOOT_REQUIRED

// --- Linux: PipeWire ------------------------------------------------------------
// Drop-in file for ~/.config/pipewire/pipewire.conf.d/.
QString pipeWireConfFileName(); // "60-vocalink-virtual-mic.conf"
QString pipeWireConfPath(const QString &configHome);
// A libpipewire-module-loopback whose capture side is an Audio/Sink ("Vocal Ink Voice")
// and whose playback side is an Audio/Source ("Vocal Ink Mic").
QString pipeWireConfText();

enum class SoundServer { Unknown, PulseAudio, PipeWire };
// Reads the output of `pactl info` ("Server Name: PulseAudio (on PipeWire 1.0.5)").
SoundServer soundServerFromPactlInfo(const QString &pactlInfo);

// --- Linux: plain PulseAudio ----------------------------------------------------
QString pulseDefaultPaPath(const QString &configHome); // <configHome>/pulse/default.pa
// The delimited block Vocal Ink manages inside default.pa (ends with a newline).
QString pulseBlock();
bool hasPulseBlock(const QString &defaultPa);
// Returns default.pa with exactly one Vocal Ink block at the end. `fileExisted` false
// means there was no per-user default.pa yet: a per-user file replaces the system one,
// so the new file first includes /etc/pulse/default.pa. Idempotent.
QString insertPulseBlock(const QString &defaultPa, bool fileExisted);
// Removes the Vocal Ink block and leaves every other line untouched.
QString removePulseBlock(const QString &defaultPa);
// True if nothing but what insertPulseBlock() created for a new file is left, so the
// file can be deleted instead of kept.
bool isOnlyGeneratedPulseHeader(const QString &defaultPa);

// --- macOS ----------------------------------------------------------------------
// Quotes a string for /bin/sh with single quotes.
QString shellQuote(const QString &s);
// Escapes a string for use inside an AppleScript "..." literal.
QString appleScriptEscape(const QString &s);
// Shell script (run as root) that copies the bundled driver into the HAL folder,
// fixes ownership, permissions and quarantine, and restarts coreaudiod.
QString macInstallShellScript(const QString &bundledDriverPath);
QString macUninstallShellScript();
// Arguments for /usr/bin/osascript: one `do shell script ... with administrator privileges`.
QStringList osascriptArguments(const QString &shellScript, const QString &prompt);
// osascript reports "User canceled. (-128)" when the password prompt is dismissed.
bool osascriptWasCancelled(int exitCode, const QString &standardError);
// CFBundleVersion from an XML Info.plist, or empty.
QString plistBundleVersion(const QByteArray &infoPlistXml);
// Compares dotted version strings numerically: <0, 0, >0.
int compareVersions(const QString &a, const QString &b);

// --- Windows (nefconw) ----------------------------------------------------------
// `nefconw install <inf> ROOT\VocalInkAudio --no-duplicates`: creates the ROOT device
// node if needed and installs/updates the driver on it (nefcon's devcon-compatible verb).
QStringList nefconInstallArguments(const QString &infPath);
// `nefconw --remove-device-node --hardware-id ROOT\VocalInkAudio --class-guid <MEDIA>`:
// removes the device and, once unused, the driver package.
QStringList nefconUninstallArguments();
// Joins arguments into one command line using the MSVC runtime quoting rules
// (what CommandLineToArgvW undoes), for ShellExecuteExW's lpParameters.
QString windowsCommandLine(const QStringList &arguments);

// --- State ----------------------------------------------------------------------
enum class Platform { Other, Linux, MacOS, Windows };
Platform currentPlatform();

struct Facts
{
    Platform platform = Platform::Other;
    // Linux: pactl or PipeWire tools found. macOS: the app bundle carries the .driver.
    // Windows: the signed package and nefconw.exe sit next to the executable.
    bool available = false;
    // Linux: the persistent config exists. macOS: the plug-in is in /Library/Audio/Plug-Ins/HAL.
    // Windows: the ROOT\VocalInkAudio device node exists.
    bool installed = false;
    // Both devices show up in the system's device lists.
    bool devicesVisible = false;
    // The last install said the OS must restart (Windows 3010) or log out (Linux without pactl).
    bool restartPending = false;
    // macOS: the bundled driver is newer than the installed one.
    bool updateAvailable = false;
};

struct StateInfo
{
    VirtualDriver::State state = VirtualDriver::State::Unsupported;
    QString text;
};

// Maps what was found on the system to the state and the user-facing sentence.
StateInfo describe(const Facts &facts);

QString outputNameFor(Platform platform);
QString inputNameFor(Platform platform);

} // namespace VirtualDriverDetail
