#include "platform/VirtualDriverDetail.h"

#include <QCoreApplication>
#include <QDir>
#include <QXmlStreamReader>

#include <algorithm>

namespace VirtualDriverDetail {

namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("VirtualDriver", s);
}

QString q(const char *s)
{
    return QString::fromUtf8(s);
}

// --- default.pa ------------------------------------------------------------------

const QString &beginMarker()
{
    static const QString s = QStringLiteral("### BEGIN Vocal Ink virtual microphone");
    return s;
}

const QString &endMarker()
{
    static const QString s = QStringLiteral("### END Vocal Ink virtual microphone");
    return s;
}

// The command lines inside the block. PulseAudio's module argument parser only honours
// quotes around a whole value, so each property list is quoted as a unit.
const QStringList &pulseBlockCommands()
{
    static const QStringList lines = {
        QStringLiteral(".nofail"),
        QStringLiteral("load-module module-null-sink sink_name=vocalink_voice "
                       "sink_properties=\"device.description='Vocal Ink Voice'\""),
        QStringLiteral("load-module module-remap-source master=vocalink_voice.monitor source_name=vocalink_mic "
                       "source_properties=\"device.description='Vocal Ink Mic'\""),
    };
    return lines;
}

const QStringList &generatedHeaderLines()
{
    static const QStringList lines = {
        QStringLiteral("# Created by Vocal Ink so its virtual microphone comes back after you log in."),
        QStringLiteral("# Everything in the system-wide default.pa still applies."),
        QStringLiteral(".include /etc/pulse/default.pa"),
    };
    return lines;
}

// Splits into lines that keep their line terminator, so joining them gives back the input.
QStringList splitKeepingEnds(const QString &text)
{
    QStringList lines;
    qsizetype start = 0;
    while (start < text.size()) {
        const qsizetype nl = text.indexOf(QLatin1Char('\n'), start);
        if (nl < 0) {
            lines << text.mid(start);
            break;
        }
        lines << text.mid(start, nl - start + 1);
        start = nl + 1;
    }
    return lines;
}

bool lineStartsWith(const QString &line, const QString &marker)
{
    return line.trimmed().startsWith(marker);
}

bool isOurCommandLine(const QString &line)
{
    return pulseBlockCommands().contains(line.trimmed());
}

} // namespace

// --- PipeWire --------------------------------------------------------------------------

QString pipeWireConfFileName()
{
    return QStringLiteral("60-vocalink-virtual-mic.conf");
}

QString pipeWireConfPath(const QString &configHome)
{
    return QDir(configHome).filePath(QStringLiteral("pipewire/pipewire.conf.d/") + pipeWireConfFileName());
}

QString pipeWireConfText()
{
    return QStringLiteral(
        "# Vocal Ink virtual microphone. Created by Vocal Ink; remove it from the app\n"
        "# (Settings > Audio) or delete this file and log out and back in.\n"
        "#\n"
        "# Apps play into \"Vocal Ink Voice\" and other apps record it from \"Vocal Ink Mic\".\n"
        "context.modules = [\n"
        "    {   name = libpipewire-module-loopback\n"
        "        args = {\n"
        "            node.description = \"Vocal Ink Virtual Mic\"\n"
        "            capture.props = {\n"
        "                node.name = \"vocalink_voice\"\n"
        "                node.description = \"Vocal Ink Voice\"\n"
        "                media.class = \"Audio/Sink\"\n"
        "                audio.position = [ FL FR ]\n"
        "            }\n"
        "            playback.props = {\n"
        "                node.name = \"vocalink_mic\"\n"
        "                node.description = \"Vocal Ink Mic\"\n"
        "                media.class = \"Audio/Source\"\n"
        "                audio.position = [ FL FR ]\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "]\n");
}

SoundServer soundServerFromPactlInfo(const QString &pactlInfo)
{
    const QStringList lines = pactlInfo.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1String("Server Name:"), Qt::CaseInsensitive))
            continue;
        if (trimmed.contains(QLatin1String("PipeWire"), Qt::CaseInsensitive))
            return SoundServer::PipeWire;
        if (trimmed.contains(QLatin1String("PulseAudio"), Qt::CaseInsensitive))
            return SoundServer::PulseAudio;
        return SoundServer::Unknown;
    }
    return SoundServer::Unknown;
}

// --- PulseAudio ------------------------------------------------------------------------

QString pulseDefaultPaPath(const QString &configHome)
{
    return QDir(configHome).filePath(QStringLiteral("pulse/default.pa"));
}

QString pulseBlock()
{
    QString block = beginMarker() + QStringLiteral(" (managed by Vocal Ink; remove it from the app)\n");
    for (const QString &line : pulseBlockCommands())
        block += line + QLatin1Char('\n');
    block += endMarker() + QLatin1Char('\n');
    return block;
}

bool hasPulseBlock(const QString &defaultPa)
{
    const QStringList lines = splitKeepingEnds(defaultPa);
    for (const QString &line : lines) {
        if (lineStartsWith(line, beginMarker()))
            return true;
    }
    return false;
}

QString removePulseBlock(const QString &defaultPa)
{
    const QStringList lines = splitKeepingEnds(defaultPa);
    QString out;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (!lineStartsWith(lines[i], beginMarker())) {
            out += lines[i];
            continue;
        }
        qsizetype end = -1;
        for (qsizetype j = i + 1; j < lines.size(); ++j) {
            if (lineStartsWith(lines[j], endMarker())) {
                end = j;
                break;
            }
            if (lineStartsWith(lines[j], beginMarker()))
                break;
        }
        if (end >= 0) {
            i = end;
            continue;
        }
        // Unterminated block (hand-edited file): drop the marker and only the lines we wrote.
        qsizetype j = i + 1;
        while (j < lines.size() && isOurCommandLine(lines[j]))
            ++j;
        i = j - 1;
    }
    return out;
}

QString insertPulseBlock(const QString &defaultPa, bool fileExisted)
{
    QString base = removePulseBlock(defaultPa);
    if (!fileExisted)
        base = generatedHeaderLines().join(QLatin1Char('\n')) + QStringLiteral("\n\n") + base;
    if (!base.isEmpty() && !base.endsWith(QLatin1Char('\n')))
        base += QLatin1Char('\n');
    return base + pulseBlock();
}

bool isOnlyGeneratedPulseHeader(const QString &defaultPa)
{
    QStringList remaining;
    const QStringList lines = defaultPa.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            remaining << trimmed;
    }
    return remaining.isEmpty() || remaining == generatedHeaderLines();
}

// --- macOS -----------------------------------------------------------------------------

QString shellQuote(const QString &s)
{
    QString quoted = s;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString appleScriptEscape(const QString &s)
{
    QString escaped = s;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return escaped;
}

QString macInstallShellScript(const QString &bundledDriverPath)
{
    const QString halDir = q(MacHalPluginDir);
    const QString target = halDir + QLatin1Char('/') + q(MacDriverBundleName);
    // Runs as root through `do shell script`: absolute tool paths only, and the script is
    // passed inline (never read from a user-writable file). `set -e` stops at the first
    // failing step; the optional steps end in `|| true`.
    const QStringList steps = {
        QStringLiteral("set -e"),
        QStringLiteral("S=") + shellQuote(bundledDriverPath),
        QStringLiteral("D=") + shellQuote(target),
        QStringLiteral("/bin/test -f \"$S/Contents/Info.plist\""),
        QStringLiteral("/bin/mkdir -p ") + shellQuote(halDir),
        QStringLiteral("/bin/rm -rf \"$D\""),
        QStringLiteral("/usr/bin/ditto \"$S\" \"$D\""),
        QStringLiteral("/usr/bin/xattr -dr com.apple.quarantine \"$D\" 2>/dev/null || true"),
        QStringLiteral("/usr/sbin/chown -R root:wheel \"$D\""),
        QStringLiteral("/usr/bin/find \"$D\" -type d -exec /bin/chmod 755 {} +"),
        QStringLiteral("/usr/bin/find \"$D\" -type f -exec /bin/chmod 644 {} +"),
        QStringLiteral("/bin/chmod 755 \"$D\"/Contents/MacOS/*"),
        QStringLiteral("/usr/bin/codesign --verify \"$D\" || { /bin/rm -rf \"$D\"; exit 1; }"),
        // SIGTERM; launchd restarts coreaudiod, which then loads the plug-in.
        QStringLiteral("/usr/bin/killall coreaudiod 2>/dev/null || true"),
    };
    return steps.join(QStringLiteral("; "));
}

QString macUninstallShellScript()
{
    const QString target = q(MacHalPluginDir) + QLatin1Char('/') + q(MacDriverBundleName);
    const QStringList steps = {
        QStringLiteral("set -e"),
        QStringLiteral("/bin/rm -rf ") + shellQuote(target),
        QStringLiteral("/usr/bin/killall coreaudiod 2>/dev/null || true"),
    };
    return steps.join(QStringLiteral("; "));
}

QStringList osascriptArguments(const QString &shellScript, const QString &prompt)
{
    const QString source = QStringLiteral("do shell script \"") + appleScriptEscape(shellScript)
                           + QStringLiteral("\" with prompt \"") + appleScriptEscape(prompt)
                           + QStringLiteral("\" with administrator privileges");
    return {QStringLiteral("-e"), source};
}

bool osascriptWasCancelled(int exitCode, const QString &standardError)
{
    return exitCode != 0 && standardError.contains(QLatin1String("(-128)"));
}

QString plistBundleVersion(const QByteArray &infoPlistXml)
{
    QXmlStreamReader xml(infoPlistXml);
    bool nextIsVersion = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == QLatin1String("key")) {
            nextIsVersion = xml.readElementText() == QLatin1String("CFBundleVersion");
            continue;
        }
        if (nextIsVersion)
            return xml.name() == QLatin1String("string") ? xml.readElementText().trimmed() : QString();
    }
    return {};
}

int compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.trimmed().split(QLatin1Char('.'));
    const QStringList pb = b.trimmed().split(QLatin1Char('.'));
    const qsizetype n = std::max(pa.size(), pb.size());
    for (qsizetype i = 0; i < n; ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

// --- Windows ---------------------------------------------------------------------------

QStringList nefconInstallArguments(const QString &infPath)
{
    return {QStringLiteral("install"), QDir::toNativeSeparators(infPath), q(WindowsHardwareId),
            QStringLiteral("--no-duplicates")};
}

QStringList nefconUninstallArguments()
{
    return {QStringLiteral("--remove-device-node"), QStringLiteral("--hardware-id"), q(WindowsHardwareId),
            QStringLiteral("--class-guid"), q(WindowsMediaClassGuid)};
}

QString windowsCommandLine(const QStringList &arguments)
{
    QStringList quoted;
    for (const QString &arg : arguments) {
        const bool needsQuotes = arg.isEmpty() || arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'))
                                 || arg.contains(QLatin1Char('\n')) || arg.contains(QLatin1Char('\v'))
                                 || arg.contains(QLatin1Char('"'));
        if (!needsQuotes) {
            quoted << arg;
            continue;
        }
        QString out = QStringLiteral("\"");
        int backslashes = 0;
        for (const QChar c : arg) {
            if (c == QLatin1Char('\\')) {
                ++backslashes;
                continue;
            }
            if (c == QLatin1Char('"')) {
                // Backslashes before a quote are doubled, and the quote itself is escaped.
                out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
                out += QLatin1Char('"');
            } else {
                out += QString(backslashes, QLatin1Char('\\'));
                out += c;
            }
            backslashes = 0;
        }
        // Backslashes before the closing quote are doubled so they stay literal.
        out += QString(backslashes * 2, QLatin1Char('\\'));
        out += QLatin1Char('"');
        quoted << out;
    }
    return quoted.join(QLatin1Char(' '));
}

// --- State -----------------------------------------------------------------------------

Platform currentPlatform()
{
#if defined(Q_OS_WIN)
    return Platform::Windows;
#elif defined(Q_OS_MACOS)
    return Platform::MacOS;
#elif defined(Q_OS_LINUX)
    return Platform::Linux;
#else
    return Platform::Other;
#endif
}

QString outputNameFor(Platform platform)
{
    return platform == Platform::MacOS ? q(MacDeviceName) : q(VoiceName);
}

QString inputNameFor(Platform platform)
{
    return platform == Platform::MacOS ? q(MacDeviceName) : q(MicName);
}

StateInfo describe(const Facts &f)
{
    using State = VirtualDriver::State;
    switch (f.platform) {
    case Platform::Linux:
        if (!f.available)
            return {State::Unsupported,
                    tr("Vocal Ink couldn't find PipeWire or PulseAudio on this computer, so it can't create "
                       "a virtual mic.")};
        if (f.installed && f.devicesVisible)
            return {State::Installed,
                    tr("Installed. Pick “Vocal Ink Mic” as the microphone in Discord or OBS. It comes back "
                       "by itself when you log in.")};
        if (f.installed && f.restartPending)
            return {State::RestartNeeded, tr("Log out and back in to finish setting up the virtual mic.")};
        if (f.devicesVisible)
            return {State::NotInstalled,
                    tr("The virtual mic works until you log out. Click Install to keep it.")};
        return {State::NotInstalled,
                tr("Not installed. Vocal Ink can create a virtual mic that stays after you log out. "
                   "No password needed.")};

    case Platform::MacOS:
        if (f.installed && f.devicesVisible) {
            if (f.available && f.updateAvailable)
                return {State::NotInstalled,
                        tr("An update to the virtual mic is available. Click Install to update it "
                           "(you'll be asked for your password).")};
            return {State::Installed,
                    tr("Installed. Pick “Vocal Ink Virtual Mic” as the microphone in Discord or OBS.")};
        }
        if (f.installed)
            return {State::RestartNeeded, tr("Your Mac needs a restart before the virtual mic appears.")};
        if (!f.available)
            return {State::Unavailable,
                    tr("This build doesn't include the macOS virtual mic. Use BlackHole instead (free).")};
        return {State::NotInstalled,
                tr("Not installed. Vocal Ink can add a virtual mic to your Mac. You'll be asked for your "
                   "password.")};

    case Platform::Windows:
        if (f.devicesVisible)
            return {State::Installed,
                    tr("Installed. Pick “Vocal Ink Mic” as the microphone in Discord or OBS.")};
        if (f.installed || f.restartPending)
            return {State::RestartNeeded, tr("Restart Windows to finish installing the virtual mic.")};
        if (!f.available)
            return {State::Unavailable,
                    tr("This build doesn't include the signed Windows driver. Use VB-CABLE instead (free).")};
        return {State::NotInstalled,
                tr("Not installed. Vocal Ink can add a virtual mic. Windows will ask for permission.")};

    case Platform::Other:
        break;
    }
    return {State::Unsupported, tr("No bundled virtual mic for this system yet.")};
}

} // namespace VirtualDriverDetail
