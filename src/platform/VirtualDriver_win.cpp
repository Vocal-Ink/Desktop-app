// Windows half of VirtualDriver: installs the signed VocalInkAudio driver package that
// ships in <exe dir>\driver with nefconw.exe, elevated through one UAC prompt.
#include "platform/VirtualDriverPlatform.h"
#include "platform/VirtualDriver.h"

#if defined(Q_OS_WIN)

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <setupapi.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace VirtualDriverPlatform {

namespace {

using namespace VirtualDriverDetail;

QString driverDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/driver");
}

QString nefconPath()
{
    return driverDir() + QStringLiteral("/nefconw.exe");
}

QString infPath()
{
    return driverDir() + QLatin1Char('/') + QString::fromUtf8(WindowsInfName);
}

bool packagePresent()
{
    const QString dir = driverDir();
    for (const char *name : {"VocalInkAudio.inf", "VocalInkAudio.sys", "VocalInkAudio.cat", "nefconw.exe"}) {
        if (!QFileInfo::exists(dir + QLatin1Char('/') + QString::fromLatin1(name)))
            return false;
    }
    return true;
}

// Is a ROOT\VocalInkAudio device node present (installed, maybe waiting for a restart)?
bool deviceNodePresent()
{
    // GUID_DEVCLASS_MEDIA, spelled out to avoid pulling in devguid.h/initguid.h.
    static const GUID mediaClass = {0x4d36e96c, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    HDEVINFO set = SetupDiGetClassDevsW(&mediaClass, L"ROOT", nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return false;
    const std::wstring wanted = QString::fromUtf8(WindowsHardwareId).toStdWString();
    bool found = false;
    SP_DEVINFO_DATA info = {};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        // REG_MULTI_SZ: strings separated by NULs, ended by an empty string.
        wchar_t ids[1024] = {};
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(ids),
                                               DWORD(sizeof(ids) - 2 * sizeof(wchar_t)), nullptr))
            continue;
        for (const wchar_t *id = ids; *id; id += std::wcslen(id) + 1) {
            if (_wcsicmp(id, wanted.c_str()) == 0) {
                found = true;
                break;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

struct Elevated
{
    enum class Status { Ran, Cancelled, Failed };
    Status status = Status::Failed;
    DWORD exitCode = 0;
    DWORD error = 0;
};

// Runs nefconw.exe elevated ("runas" shows the UAC prompt) and waits for it.
// Worker thread only.
Elevated runElevated(const QStringList &arguments)
{
    Elevated out;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const std::wstring file = QDir::toNativeSeparators(nefconPath()).toStdWString();
    const std::wstring params = windowsCommandLine(arguments).toStdWString();
    const std::wstring dir = QDir::toNativeSeparators(driverDir()).toStdWString();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.c_str();
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        out.error = GetLastError();
        out.status = out.error == ERROR_CANCELLED ? Elevated::Status::Cancelled : Elevated::Status::Failed;
    } else if (!sei.hProcess) {
        out.status = Elevated::Status::Failed;
    } else {
        WaitForSingleObject(sei.hProcess, INFINITE);
        if (GetExitCodeProcess(sei.hProcess, &out.exitCode))
            out.status = Elevated::Status::Ran;
        else
            out.error = GetLastError();
        CloseHandle(sei.hProcess);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return out;
}

Result unavailable()
{
    Facts facts;
    facts.platform = Platform::Windows;
    Result result;
    result.message = describe(facts).text;
    return result;
}

} // namespace

VirtualDriverDetail::Facts probe()
{
    Facts facts;
    facts.platform = Platform::Windows;
    facts.available = packagePresent();
    facts.installed = deviceNodePresent();
    facts.devicesVisible = devicesVisible(QString::fromUtf8(VoiceName), QString::fromUtf8(MicName));
    return facts;
}

Result install()
{
    if (!packagePresent())
        return unavailable();
    Result result;
    const Elevated run = runElevated(nefconInstallArguments(QDir::toNativeSeparators(infPath())));
    switch (run.status) {
    case Elevated::Status::Cancelled:
        result.outcome = Result::Outcome::Cancelled;
        result.message = VirtualDriver::tr("Installation cancelled.");
        return result;
    case Elevated::Status::Failed:
        result.message = VirtualDriver::tr("Couldn't start the driver installer (error %1).").arg(run.error);
        return result;
    case Elevated::Status::Ran:
        break;
    }
    if (run.exitCode == DWORD(WindowsRebootRequired)) {
        result.outcome = Result::Outcome::OkRestartNeeded;
        result.message = VirtualDriver::tr("Restart Windows to finish installing the virtual mic.");
        return result;
    }
    if (run.exitCode != 0) {
        result.message = VirtualDriver::tr("The driver installer failed (error %1).").arg(run.exitCode);
        return result;
    }
    result.outcome = Result::Outcome::Ok;
    result.message = VirtualDriver::tr("Installed. Pick “Vocal Ink Mic” as the microphone in Discord or OBS.");
    result.wait = Result::Wait::DevicesAppear;
    result.waitSeconds = 10;
    result.timeoutMessage = VirtualDriver::tr("The driver is installed. Restart Windows to finish setting up the virtual mic.");
    result.restartOnTimeout = true;
    return result;
}

Result uninstall()
{
    Result result;
    if (!QFileInfo::exists(nefconPath())) {
        result.message = VirtualDriver::tr("This copy of Vocal Ink can't remove the driver (nefconw.exe is missing). Remove "
                            "“Vocal Ink Virtual Audio Device” in Device Manager instead.");
        return result;
    }
    const Elevated run = runElevated(nefconUninstallArguments());
    switch (run.status) {
    case Elevated::Status::Cancelled:
        result.outcome = Result::Outcome::Cancelled;
        result.message = VirtualDriver::tr("Removal cancelled.");
        return result;
    case Elevated::Status::Failed:
        result.message = VirtualDriver::tr("Couldn't start the driver installer (error %1).").arg(run.error);
        return result;
    case Elevated::Status::Ran:
        break;
    }
    if (run.exitCode == DWORD(WindowsRebootRequired)) {
        result.outcome = Result::Outcome::Ok;
        result.message = VirtualDriver::tr("Restart Windows to finish removing the virtual mic.");
        return result;
    }
    // nefcon fails when there was nothing to remove; that's fine.
    if (run.exitCode != 0 && deviceNodePresent()) {
        result.message = VirtualDriver::tr("Couldn't remove the driver (error %1).").arg(run.exitCode);
        return result;
    }
    result.outcome = Result::Outcome::Ok;
    result.message = VirtualDriver::tr("The virtual mic was removed.");
    result.wait = Result::Wait::DevicesDisappear;
    result.waitSeconds = 10;
    result.timeoutMessage = VirtualDriver::tr("The virtual mic was removed. Restart Windows if it still shows up.");
    return result;
}

} // namespace VirtualDriverPlatform

#endif // Q_OS_WIN
