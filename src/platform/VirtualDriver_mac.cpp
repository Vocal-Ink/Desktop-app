// macOS half of VirtualDriver: copies VocalInkVirtualMic.driver (a Core Audio HAL
// plug-in shipped in Vocal Ink.app/Contents/Resources) to /Library/Audio/Plug-Ins/HAL
// behind one administrator password prompt, and restarts coreaudiod.
#include "platform/VirtualDriverPlatform.h"

#if defined(Q_OS_MACOS)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace VirtualDriverPlatform {

namespace {

using namespace VirtualDriverDetail;

QString tr(const char *s)
{
    return QCoreApplication::translate("VirtualDriver", s);
}

QString bundledDriverPath()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/")
                           + QString::fromUtf8(MacDriverBundleName));
}

QString installedDriverPath()
{
    return QString::fromUtf8(MacHalPluginDir) + QLatin1Char('/') + QString::fromUtf8(MacDriverBundleName);
}

QString bundleVersion(const QString &bundlePath)
{
    QFile plist(bundlePath + QStringLiteral("/Contents/Info.plist"));
    if (!plist.open(QIODevice::ReadOnly))
        return {};
    return plistBundleVersion(plist.readAll());
}

bool isBundle(const QString &bundlePath)
{
    return QFileInfo::exists(bundlePath + QStringLiteral("/Contents/Info.plist"));
}

// Runs `script` as root through osascript's standard password prompt. Blocks until the
// prompt is answered and the script ends, so it only runs on a worker thread.
Result runAsAdministrator(const QString &script, const QString &prompt, const QString &cancelledMessage,
                          const QString &failedMessage)
{
    Result result;
    QProcess p;
    p.start(QStringLiteral("/usr/bin/osascript"), osascriptArguments(script, prompt));
    if (!p.waitForStarted(10000)) {
        result.message = failedMessage.arg(p.errorString());
        return result;
    }
    p.waitForFinished(-1);
    QString stderrText = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        result.outcome = Result::Outcome::Ok;
        return result;
    }
    if (osascriptWasCancelled(p.exitCode(), stderrText)) {
        result.outcome = Result::Outcome::Cancelled;
        result.message = cancelledMessage;
        return result;
    }
    // "0:117: execution error: <message> (1)" -> "<message> (1)"
    const QLatin1String marker("execution error: ");
    const qsizetype at = stderrText.indexOf(marker);
    if (at >= 0)
        stderrText = stderrText.mid(at + marker.size());
    if (stderrText.isEmpty())
        stderrText = tr("osascript exited with code %1").arg(p.exitCode());
    result.message = failedMessage.arg(stderrText);
    return result;
}

} // namespace

VirtualDriverDetail::Facts probe()
{
    Facts facts;
    facts.platform = Platform::MacOS;
    const QString bundled = bundledDriverPath();
    const QString installed = installedDriverPath();
    facts.available = isBundle(bundled);
    facts.installed = isBundle(installed);
    const QString device = QString::fromUtf8(MacDeviceName);
    facts.devicesVisible = devicesVisible(device, device);
    if (facts.available && facts.installed)
        facts.updateAvailable = compareVersions(bundleVersion(bundled), bundleVersion(installed)) > 0;
    return facts;
}

Result install()
{
    const QString bundled = bundledDriverPath();
    if (!isBundle(bundled)) {
        Result result;
        Facts facts;
        facts.platform = Platform::MacOS;
        result.message = describe(facts).text;
        return result;
    }
    Result result = runAsAdministrator(macInstallShellScript(bundled),
                                       tr("Vocal Ink needs your password to install its virtual microphone."),
                                       tr("Installation cancelled."),
                                       tr("Couldn't install the virtual mic: %1"));
    if (result.outcome != Result::Outcome::Ok)
        return result;
    // coreaudiod restarts and loads the plug-in; give Qt's device list time to catch up.
    result.message = tr("Installed. Pick “Vocal Ink Virtual Mic” as the microphone in Discord or OBS.");
    result.wait = Result::Wait::DevicesAppear;
    result.waitSeconds = 10;
    result.timeoutMessage = tr("Installed. Your Mac needs a restart before the virtual mic appears.");
    return result;
}

Result uninstall()
{
    Result result = runAsAdministrator(macUninstallShellScript(),
                                       tr("Vocal Ink needs your password to remove its virtual microphone."),
                                       tr("Removal cancelled."),
                                       tr("Couldn't remove the virtual mic: %1"));
    if (result.outcome != Result::Outcome::Ok)
        return result;
    result.message = tr("The virtual mic was removed.");
    result.wait = Result::Wait::DevicesDisappear;
    result.waitSeconds = 10;
    result.timeoutMessage = tr("The virtual mic was removed. It disappears completely after a restart.");
    return result;
}

} // namespace VirtualDriverPlatform

#endif // Q_OS_MACOS
