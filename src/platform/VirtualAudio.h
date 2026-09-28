#pragma once

#include <QByteArray>
#include <QString>

// Helpers for routing the voice into other apps. Other apps can only "hear"
// Vocal Ink through a virtual audio cable: Vocal Ink plays into the cable's
// input and Discord/OBS/games use the cable's output as their microphone.
namespace VirtualAudio {

// Rich-text setup instructions for this OS (with download links).
QString setupInstructions();

// Linux (PulseAudio/PipeWire) can create a virtual microphone on the fly.
bool canCreateVirtualMic();
bool virtualMicExists();
bool createVirtualMic(QString *error);
bool removeVirtualMic(QString *error);

// Id of an output device that looks like a virtual cable input, or empty if none is
// installed. Vocal Ink's own devices ("Vocal Ink Voice", macOS "Vocal Ink Virtual Mic")
// win over third-party cables ("CABLE Input", "BlackHole"...).
QByteArray detectVirtualCableOutput();
bool looksLikeVirtualCable(const QString &deviceDescription);

inline constexpr auto LinuxSinkName = "vocalink_voice";
inline constexpr auto LinuxSourceName = "vocalink_mic";

} // namespace VirtualAudio
