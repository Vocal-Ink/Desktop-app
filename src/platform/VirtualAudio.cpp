#include "platform/VirtualAudio.h"

#include <QAudioDevice>
#include <algorithm>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace VirtualAudio {

namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("VirtualAudio", s);
}

bool runPactl(const QStringList &args, QString *output, QString *error)
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (pactl.isEmpty()) {
        if (error)
            *error = tr("pactl was not found. Install pulseaudio-utils (works with PipeWire too).");
        return false;
    }
    QProcess p;
    p.start(pactl, args);
    if (!p.waitForFinished(8000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (error && error->isEmpty())
            *error = tr("pactl %1 failed").arg(args.value(0));
        return false;
    }
    if (output)
        *output = QString::fromUtf8(p.readAllStandardOutput());
    return true;
}

} // namespace

QString setupInstructions()
{
#if defined(Q_OS_WIN)
    return tr("<p>Install the free <b>VB-CABLE</b> virtual audio device from "
              "<a href=\"https://vb-audio.com/Cable/\">vb-audio.com/Cable</a> and restart Vocal Ink.</p>"
              "<ol><li>Choose <b>CABLE Input (VB-Audio Virtual Cable)</b> as Vocal Ink's <i>voice output</i>.</li>"
              "<li>In Discord, games or OBS choose <b>CABLE Output (VB-Audio Virtual Cable)</b> as the microphone.</li>"
              "<li>Keep <i>Also play on my speakers</i> on to hear yourself.</li></ol>");
#elif defined(Q_OS_MACOS)
    return tr("<p>Install the free <b>BlackHole 2ch</b> virtual audio driver from "
              "<a href=\"https://existential.audio/blackhole/\">existential.audio/blackhole</a> "
              "(or <code>brew install blackhole-2ch</code>) and restart Vocal Ink.</p>"
              "<ol><li>Choose <b>BlackHole 2ch</b> as Vocal Ink's <i>voice output</i>.</li>"
              "<li>In Discord, games or OBS choose <b>BlackHole 2ch</b> as the microphone.</li>"
              "<li>Keep <i>Also play on my speakers</i> on to hear yourself.</li></ol>");
#else
    return tr("<p>Vocal Ink can create a virtual microphone for you (PulseAudio or PipeWire). "
              "Click <b>Create virtual microphone</b>, then:</p>"
              "<ol><li>Choose <b>Vocal Ink Voice</b> as Vocal Ink's <i>voice output</i>.</li>"
              "<li>In Discord, games or OBS choose <b>Vocal Ink Mic</b> as the microphone "
              "(OBS can also capture <i>Monitor of Vocal Ink Voice</i>).</li>"
              "<li>Keep <i>Also play on my speakers</i> on to hear yourself.</li></ol>"
              "<p>The virtual devices last until you log out; Vocal Ink recreates them on request.</p>");
#endif
}

bool canCreateVirtualMic()
{
#if defined(Q_OS_LINUX)
    return !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
#else
    return false;
#endif
}

bool virtualMicExists()
{
    QString out;
    if (!runPactl({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sinks")}, &out, nullptr))
        return false;
    return out.contains(QLatin1String(LinuxSinkName));
}

bool createVirtualMic(QString *error)
{
    if (!canCreateVirtualMic()) {
        if (error)
            *error = tr("Creating a virtual microphone is only supported on Linux with PulseAudio or PipeWire.");
        return false;
    }
    if (virtualMicExists())
        return true;
    const QString sink = QString::fromLatin1(LinuxSinkName);
    // Module arguments only honour quotes around a whole value, so the property
    // list is quoted as a unit: sink_properties="device.description='A B'".
    if (!runPactl({QStringLiteral("load-module"), QStringLiteral("module-null-sink"),
                   QStringLiteral("sink_name=") + sink,
                   QStringLiteral("sink_properties=\"device.description='Vocal Ink Voice'\"")},
                  nullptr, error))
        return false;
    if (!runPactl({QStringLiteral("load-module"), QStringLiteral("module-remap-source"),
                   QStringLiteral("master=") + sink + QStringLiteral(".monitor"),
                   QStringLiteral("source_name=") + QString::fromLatin1(LinuxSourceName),
                   QStringLiteral("source_properties=\"device.description='Vocal Ink Mic'\"")},
                  nullptr, error))
        return false;
    return true;
}

bool removeVirtualMic(QString *error)
{
    QString out;
    if (!runPactl({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("modules")}, &out, error))
        return false;
    QStringList ids;
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.contains(QLatin1String(LinuxSinkName)))
            ids << line.section(QLatin1Char('\t'), 0, 0).trimmed();
    }
    // Unload the microphone (loaded last) before the sink it listens to; PulseAudio
    // may also drop it by itself when the sink goes, which is fine.
    std::reverse(ids.begin(), ids.end());
    for (const QString &id : std::as_const(ids)) {
        if (!id.isEmpty())
            runPactl({QStringLiteral("unload-module"), id}, nullptr, nullptr);
    }
    if (virtualMicExists()) {
        if (error)
            *error = tr("Could not remove the virtual microphone.");
        return false;
    }
    return true;
}

bool looksLikeVirtualCable(const QString &description)
{
    static const QStringList needles = {
        // Vocal Ink's own bundled mic: "Vocal Ink Voice" on Windows/Linux, one
        // "Vocal Ink Virtual Mic" device on macOS.
        QStringLiteral("Vocal Ink Voice"), QStringLiteral("Vocal Ink Virtual Mic"),
        QStringLiteral("CABLE Input"), QStringLiteral("VB-Audio"), QStringLiteral("BlackHole"),
        QStringLiteral("Loopback"), QStringLiteral("VoiceMeeter Input"), QStringLiteral("Virtual"),
    };
    for (const QString &n : needles) {
        if (description.contains(n, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QByteArray detectVirtualCableOutput()
{
    const auto outputs = QMediaDevices::audioOutputs();
    // Prefer Vocal Ink's own bundled mic, then well-known third-party cables, then the
    // generic "Virtual" match.
    for (const auto *preferred : {"Vocal Ink Voice", "Vocal Ink Virtual Mic", "CABLE Input", "BlackHole"}) {
        for (const QAudioDevice &dev : outputs) {
            if (dev.description().contains(QLatin1String(preferred), Qt::CaseInsensitive))
                return dev.id();
        }
    }
    for (const QAudioDevice &dev : outputs) {
        if (looksLikeVirtualCable(dev.description()))
            return dev.id();
    }
    return {};
}

} // namespace VirtualAudio
