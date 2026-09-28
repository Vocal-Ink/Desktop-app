// Linux half of VirtualDriver: a persistent PipeWire loopback or PulseAudio null
// sink + remapped source, created without administrator rights.
#include "platform/VirtualDriverPlatform.h"

#if defined(Q_OS_LINUX)

#include "platform/VirtualAudio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace VirtualDriverPlatform {

namespace {

using namespace VirtualDriverDetail;

QString tr(const char *s)
{
    return QCoreApplication::translate("VirtualDriver", s);
}

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

bool havePactl()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
}

bool havePipeWireTools()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-cli")).isEmpty()
           || !QStandardPaths::findExecutable(QStringLiteral("pipewire")).isEmpty();
}

bool readFile(const QString &path, QString *text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *text = QString::fromUtf8(file.readAll());
    return true;
}

bool writeFile(const QString &path, const QString &text, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = tr("Couldn't create the folder for %1.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        *error = tr("Couldn't save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

bool pulseBlockPresent()
{
    QString text;
    return readFile(pulseDefaultPaPath(configHome()), &text) && hasPulseBlock(text);
}

bool runPactl(const QStringList &args, QString *output)
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (pactl.isEmpty())
        return false;
    QProcess p;
    p.start(pactl, args);
    if (!p.waitForFinished(8000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return false;
    if (output)
        *output = QString::fromUtf8(p.readAllStandardOutput());
    return true;
}

// `pactl list short sinks|sources` prints "<index>\t<name>\t...".
bool pactlListsName(const QString &kind, const QString &name)
{
    QString out;
    if (!runPactl({QStringLiteral("list"), QStringLiteral("short"), kind}, &out))
        return false;
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.section(QLatin1Char('\t'), 1, 1).trimmed() == name)
            return true;
    }
    return false;
}

bool pactlSeesDevices()
{
    return pactlListsName(QStringLiteral("sinks"), QString::fromLatin1(VirtualAudio::LinuxSinkName))
           && pactlListsName(QStringLiteral("sources"), QString::fromLatin1(VirtualAudio::LinuxSourceName));
}

QString readyMessage()
{
    return tr("The virtual mic is ready. Pick “Vocal Ink Mic” as the microphone in Discord or OBS.");
}

} // namespace

VirtualDriverDetail::Facts probe()
{
    Facts facts;
    facts.platform = Platform::Linux;
    facts.available = havePactl() || havePipeWireTools();
    facts.installed = QFileInfo::exists(pipeWireConfPath(configHome())) || pulseBlockPresent();
    facts.devicesVisible = devicesVisible(QString::fromUtf8(VoiceName), QString::fromUtf8(MicName));
    return facts;
}

Result install()
{
    Result result;
    const QString home = configHome();
    const bool pactl = havePactl();

    SoundServer server = SoundServer::Unknown;
    QString info;
    if (pactl && runPactl({QStringLiteral("info")}, &info))
        server = soundServerFromPactlInfo(info);
    if (server == SoundServer::Unknown && havePipeWireTools())
        server = SoundServer::PipeWire;
    if (server == SoundServer::Unknown) {
        result.message = tr("Vocal Ink couldn't reach PulseAudio or PipeWire. Check that sound works on this "
                            "computer, then try again.");
        return result;
    }

    // 1. Make it permanent.
    QString error;
    if (server == SoundServer::PipeWire) {
        if (!writeFile(pipeWireConfPath(home), pipeWireConfText(), &error)) {
            result.message = error;
            return result;
        }
    } else {
        const QString path = pulseDefaultPaPath(home);
        QString current;
        const bool existed = readFile(path, &current);
        if (!writeFile(path, insertPulseBlock(current, existed), &error)) {
            result.message = error;
            return result;
        }
    }

    // 2. Create it for this session too, so nobody has to log out.
    if (!pactl) {
        result.outcome = Result::Outcome::OkRestartNeeded;
        result.message = tr("Log out and back in to finish setting up the virtual mic.");
        return result;
    }
    if (!pactlSeesDevices() && !VirtualAudio::createVirtualMic(&error)) {
        result.message = tr("The virtual mic was saved but couldn't be started now: %1").arg(error);
        return result;
    }
    result.outcome = Result::Outcome::Ok;
    result.message = readyMessage();
    result.wait = Result::Wait::DevicesAppear;
    result.waitSeconds = 5;
    return result;
}

Result uninstall()
{
    Result result;
    const QString home = configHome();

    const QString confPath = pipeWireConfPath(home);
    const bool hadPipeWireConf = QFileInfo::exists(confPath);
    if (hadPipeWireConf && !QFile::remove(confPath)) {
        result.message = tr("Couldn't delete %1.").arg(confPath);
        return result;
    }

    const QString paPath = pulseDefaultPaPath(home);
    QString current;
    if (readFile(paPath, &current) && hasPulseBlock(current)) {
        const QString updated = removePulseBlock(current);
        QString error;
        if (isOnlyGeneratedPulseHeader(updated)) {
            if (!QFile::remove(paPath)) {
                result.message = tr("Couldn't delete %1.").arg(paPath);
                return result;
            }
        } else if (!writeFile(paPath, updated, &error)) {
            result.message = error;
            return result;
        }
    }

    // Remove it from the running session as well (also removes a mic made with the
    // older "Create virtual microphone" button).
    if (havePactl()) {
        QString ignored;
        VirtualAudio::removeVirtualMic(&ignored);
    }

    result.outcome = Result::Outcome::Ok;
    result.message = tr("The virtual mic was removed.");
    result.wait = Result::Wait::DevicesDisappear;
    result.waitSeconds = 5;
    // A PipeWire loopback loaded at log-in can't be unloaded from outside; it goes with the session.
    result.timeoutMessage = tr("The virtual mic was removed. It disappears completely the next time you log in.");
    if (havePactl() && pactlSeesDevices())
        result.message = result.timeoutMessage;
    return result;
}

} // namespace VirtualDriverPlatform

#endif // Q_OS_LINUX
