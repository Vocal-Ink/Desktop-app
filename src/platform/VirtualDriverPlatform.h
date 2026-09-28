#pragma once

#include "platform/VirtualDriverDetail.h"

#include <QString>

// Per-OS half of VirtualDriver (VirtualDriver_linux.cpp, VirtualDriver_mac.cpp,
// VirtualDriver_win.cpp). Internal to src/platform.
namespace VirtualDriverPlatform {

struct Result
{
    enum class Outcome { Ok, OkRestartNeeded, Cancelled, Failed };
    enum class Wait { None, DevicesAppear, DevicesDisappear };

    Outcome outcome = Outcome::Failed;
    QString message; // user-facing, reported through VirtualDriver::finished()

    // Before reporting success, wait (up to waitSeconds) for the device lists to change.
    Wait wait = Wait::None;
    int waitSeconds = 10;
    QString timeoutMessage;        // reported instead when the wait times out
    bool restartOnTimeout = false; // and then the state becomes RestartNeeded
};

// GUI thread. Cheap: file checks and the cached device lists; never waits on a process.
VirtualDriverDetail::Facts probe();

// Worker thread. May block for a long time (password or UAC prompt, pactl).
// Must not touch QObjects that live on the GUI thread.
Result install();
Result uninstall();

// True if Qt lists an output whose description contains `outputName` and an input whose
// description contains `inputName`. GUI thread (implemented in VirtualDriver.cpp).
bool devicesVisible(const QString &outputName, const QString &inputName);

} // namespace VirtualDriverPlatform
