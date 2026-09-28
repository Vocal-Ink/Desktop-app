#include "platform/VirtualDriver.h"
#include "platform/VirtualDriverDetail.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace VirtualDriverDetail;
using State = VirtualDriver::State;

namespace {

// Undoes appleScriptEscape(): the escapes AppleScript understands inside "...".
QString appleScriptUnescape(const QString &s)
{
    QString out;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('\\') && i + 1 < s.size()) {
            ++i;
            out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

// CommandLineToArgvW / MSVC runtime parsing rules, to round-trip windowsCommandLine().
QStringList parseWindowsCommandLine(const QString &line)
{
    QStringList args;
    QString current;
    bool inQuotes = false;
    bool haveArg = false;
    for (qsizetype i = 0; i < line.size();) {
        const QChar c = line[i];
        if (c == QLatin1Char('\\')) {
            qsizetype n = 0;
            while (i < line.size() && line[i] == QLatin1Char('\\')) {
                ++n;
                ++i;
            }
            if (i < line.size() && line[i] == QLatin1Char('"')) {
                current += QString(n / 2, QLatin1Char('\\'));
                if (n % 2) {
                    current += QLatin1Char('"');
                    ++i;
                }
            } else {
                current += QString(n, QLatin1Char('\\'));
            }
            haveArg = true;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            haveArg = true;
            ++i;
            continue;
        }
        if (!inQuotes && (c == QLatin1Char(' ') || c == QLatin1Char('\t'))) {
            if (haveArg)
                args << current;
            current.clear();
            haveArg = false;
            ++i;
            continue;
        }
        current += c;
        haveArg = true;
        ++i;
    }
    if (haveArg)
        args << current;
    return args;
}

bool haveShell()
{
    return QFile::exists(QStringLiteral("/bin/sh"));
}

QString runShell(const QString &script, int *exitCode = nullptr)
{
    QProcess p;
    p.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
    p.waitForFinished(10000);
    if (exitCode)
        *exitCode = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput());
}

bool pulseServerRunning()
{
    if (QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
        return false;
    QProcess p;
    p.start(QStringLiteral("pactl"), {QStringLiteral("info")});
    return p.waitForFinished(5000) && p.exitCode() == 0;
}

} // namespace

class TestVirtualDriver : public QObject
{
    Q_OBJECT
private slots:
    // --- Linux: PipeWire -----------------------------------------------------------

    void pipeWireConfDefinesTheLoopbackPair()
    {
        const QString conf = pipeWireConfText();
        QVERIFY(conf.contains(QLatin1String("context.modules = [")));
        QVERIFY(conf.contains(QLatin1String("name = libpipewire-module-loopback")));
        QVERIFY(conf.contains(QLatin1String("capture.props = {")));
        QVERIFY(conf.contains(QLatin1String("playback.props = {")));
        QVERIFY(conf.contains(QLatin1String("node.name = \"vocalink_voice\"")));
        QVERIFY(conf.contains(QLatin1String("node.description = \"Vocal Ink Voice\"")));
        QVERIFY(conf.contains(QLatin1String("media.class = \"Audio/Sink\"")));
        QVERIFY(conf.contains(QLatin1String("node.name = \"vocalink_mic\"")));
        QVERIFY(conf.contains(QLatin1String("node.description = \"Vocal Ink Mic\"")));
        QVERIFY(conf.contains(QLatin1String("media.class = \"Audio/Source\"")));
        QCOMPARE(conf.count(QLatin1String("audio.position = [ FL FR ]")), 2);
        // The sink half comes first (capture side), the source half second.
        QVERIFY(conf.indexOf(QLatin1String("Audio/Sink")) < conf.indexOf(QLatin1String("Audio/Source")));
        QCOMPARE(conf.count(QLatin1Char('{')), conf.count(QLatin1Char('}')));
        QCOMPARE(conf.count(QLatin1Char('[')), conf.count(QLatin1Char(']')));
        QCOMPARE(conf.count(QLatin1Char('"')) % 2, 0);
    }

    void pipeWireConfLivesInTheDropInFolder()
    {
        QCOMPARE(pipeWireConfPath(QStringLiteral("/home/me/.config")),
                 QStringLiteral("/home/me/.config/pipewire/pipewire.conf.d/60-vocalink-virtual-mic.conf"));
        QCOMPARE(pulseDefaultPaPath(QStringLiteral("/home/me/.config")),
                 QStringLiteral("/home/me/.config/pulse/default.pa"));
    }

    void detectsTheSoundServer_data()
    {
        QTest::addColumn<QString>("info");
        QTest::addColumn<int>("server");
        QTest::newRow("pipewire") << QStringLiteral(
            "Server String: /run/user/1000/pulse/native\nLibrary Protocol Version: 35\nIs Local: yes\n"
            "User Name: me\nServer Name: PulseAudio (on PipeWire 1.0.5)\nServer Version: 15.0.0\n")
                                  << int(SoundServer::PipeWire);
        QTest::newRow("pulseaudio") << QStringLiteral("Server Name: pulseaudio\nServer Version: 16.1\n")
                                    << int(SoundServer::PulseAudio);
        QTest::newRow("empty") << QString() << int(SoundServer::Unknown);
        QTest::newRow("other") << QStringLiteral("Server Name: something else\n") << int(SoundServer::Unknown);
    }

    void detectsTheSoundServer()
    {
        QFETCH(QString, info);
        QFETCH(int, server);
        QCOMPARE(int(soundServerFromPactlInfo(info)), server);
    }

    // --- Linux: PulseAudio default.pa ----------------------------------------------

    void newDefaultPaIncludesTheSystemFile()
    {
        const QString text = insertPulseBlock(QString(), false);
        const qsizetype include = text.indexOf(QLatin1String("\n.include /etc/pulse/default.pa\n"));
        QVERIFY(include >= 0);
        QVERIFY(include < text.indexOf(QLatin1String("### BEGIN Vocal Ink virtual microphone")));
        QVERIFY(hasPulseBlock(text));
        QVERIFY(text.endsWith(QLatin1String("### END Vocal Ink virtual microphone\n")));
        QVERIFY(text.contains(QLatin1String(
            "load-module module-null-sink sink_name=vocalink_voice "
            "sink_properties=\"device.description='Vocal Ink Voice'\"\n")));
        QVERIFY(text.contains(QLatin1String(
            "load-module module-remap-source master=vocalink_voice.monitor source_name=vocalink_mic "
            "source_properties=\"device.description='Vocal Ink Mic'\"\n")));
        // A broken line must not stop PulseAudio from starting.
        QVERIFY(text.indexOf(QLatin1String(".nofail")) < text.indexOf(QLatin1String("load-module")));
    }

    void insertingIsIdempotent()
    {
        const QString once = insertPulseBlock(QString(), false);
        QCOMPARE(insertPulseBlock(once, true), once);
        QCOMPARE(insertPulseBlock(insertPulseBlock(once, true), true), once);
        QCOMPARE(once.count(QLatin1String("### BEGIN Vocal Ink virtual microphone")), 1);

        const QString user = QStringLiteral(".include /etc/pulse/default.pa\nload-module module-echo-cancel\n");
        const QString withBlock = insertPulseBlock(user, true);
        QCOMPARE(insertPulseBlock(withBlock, true), withBlock);
    }

    void removingKeepsTheUsersLines()
    {
        const QString user = QStringLiteral(".include /etc/pulse/default.pa\n"
                                            "# my own tweaks\n"
                                            "load-module module-echo-cancel aec_method=webrtc\n"
                                            "set-default-source echo-cancel-source\n");
        const QString withBlock = insertPulseBlock(user, true);
        QVERIFY(withBlock.startsWith(user));
        QVERIFY(hasPulseBlock(withBlock));
        QCOMPARE(removePulseBlock(withBlock), user);
        QVERIFY(!hasPulseBlock(removePulseBlock(withBlock)));
        QCOMPARE(removePulseBlock(removePulseBlock(withBlock)), user);
        QCOMPARE(removePulseBlock(user), user);
        QVERIFY(!isOnlyGeneratedPulseHeader(removePulseBlock(withBlock)));
    }

    void removingKeepsLinesAfterTheBlock()
    {
        const QString before = QStringLiteral("load-module module-a\n");
        const QString after = QStringLiteral("load-module module-b\r\nset-default-sink x\r\n");
        const QString text = insertPulseBlock(before, true) + after;
        QCOMPARE(removePulseBlock(text), before + after);
    }

    void addsAMissingFinalNewline()
    {
        const QString user = QStringLiteral("load-module module-a");
        const QString withBlock = insertPulseBlock(user, true);
        QVERIFY(withBlock.startsWith(user + QLatin1Char('\n') + QLatin1String("### BEGIN")));
        QCOMPARE(removePulseBlock(withBlock), user + QLatin1Char('\n'));
    }

    void keepsWindowsLineEndings()
    {
        const QString user = QStringLiteral(".include /etc/pulse/default.pa\r\nload-module module-a\r\n");
        QCOMPARE(removePulseBlock(insertPulseBlock(user, true)), user);
    }

    void removesAnUnterminatedBlockCarefully()
    {
        QString block = pulseBlock();
        block.remove(QLatin1String("### END Vocal Ink virtual microphone\n"));
        const QString user = QStringLiteral("load-module module-a\n");
        const QString tail = QStringLiteral("load-module module-user-added-later\n");
        QCOMPARE(removePulseBlock(user + block + tail), user + tail);
    }

    void generatedFileCanBeDeletedAgain()
    {
        const QString created = insertPulseBlock(QString(), false);
        QVERIFY(isOnlyGeneratedPulseHeader(removePulseBlock(created)));
        QVERIFY(isOnlyGeneratedPulseHeader(QString()));
        QVERIFY(!isOnlyGeneratedPulseHeader(removePulseBlock(created) + QStringLiteral("load-module x\n")));
    }

    // --- macOS -----------------------------------------------------------------------

    void shellQuotingSurvivesTheShell_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("plain") << QStringLiteral("/Applications/Vocal Ink.app/Contents/Resources/X.driver");
        QTest::newRow("single quote") << QStringLiteral("/Users/o'brien/Apps/Vocal Ink.app/X.driver");
        QTest::newRow("double quote") << QStringLiteral("/Volumes/My \"Stuff\"/Vocal Ink.app");
        QTest::newRow("specials") << QStringLiteral("/tmp/$HOME `id` \\ ; & | * ? ! {} [x]");
        QTest::newRow("unicode") << QStringLiteral("/Users/zoë/Programme/Vocal Ink.app");
    }

    void shellQuotingSurvivesTheShell()
    {
        QFETCH(QString, path);
        if (!haveShell())
            QSKIP("needs /bin/sh");
        // macOS hands arguments to the shell decomposed (NFD): compare canonical forms.
        const QString echoed = runShell(QStringLiteral("S=") + shellQuote(path) + QStringLiteral("; printf %s \"$S\""));
        QCOMPARE(echoed.normalized(QString::NormalizationForm_C), path.normalized(QString::NormalizationForm_C));
    }

    void appleScriptEscapingRoundTrips()
    {
        const QString nasty = QStringLiteral("a \"quoted\" \\back\\slash\\ and 'single' \\\"mix\\\"");
        const QString escaped = appleScriptEscape(nasty);
        QVERIFY(!escaped.contains(QRegularExpression(QStringLiteral("(^|[^\\\\])(\\\\\\\\)*\""))));
        QCOMPARE(appleScriptUnescape(escaped), nasty);
    }

    void installScriptCopiesFixesAndRestarts()
    {
        const QString src = QStringLiteral("/Users/o'brien/Down loads/Vocal \"Ink\".app/Contents/Resources/"
                                           "VocalInkVirtualMic.driver");
        const QString script = macInstallShellScript(src);
        QVERIFY(script.startsWith(QLatin1String("set -e; ")));
        QVERIFY(script.contains(QStringLiteral("S=") + shellQuote(src) + QLatin1Char(';')));
        QVERIFY(script.contains(QLatin1String("D='/Library/Audio/Plug-Ins/HAL/VocalInkVirtualMic.driver'")));
        QVERIFY(script.contains(QLatin1String("/bin/mkdir -p '/Library/Audio/Plug-Ins/HAL'")));
        QVERIFY(script.contains(QLatin1String("/bin/rm -rf \"$D\"")));
        QVERIFY(script.contains(QLatin1String("/usr/bin/ditto \"$S\" \"$D\"")));
        QVERIFY(script.contains(QLatin1String("/usr/bin/xattr -dr com.apple.quarantine \"$D\"")));
        QVERIFY(script.contains(QLatin1String("/usr/sbin/chown -R root:wheel \"$D\"")));
        QVERIFY(script.contains(QLatin1String("-type d -exec /bin/chmod 755 {} +")));
        QVERIFY(script.contains(QLatin1String("-type f -exec /bin/chmod 644 {} +")));
        QVERIFY(script.contains(QLatin1String("/bin/chmod 755 \"$D\"/Contents/MacOS/*")));
        QVERIFY(script.contains(QLatin1String("/usr/bin/killall coreaudiod")));
        QVERIFY(!script.contains(QLatin1String("launchctl")));
        QVERIFY(!script.contains(QLatin1String("-9")));
        // Copy before restarting the audio server.
        QVERIFY(script.indexOf(QLatin1String("ditto")) < script.indexOf(QLatin1String("killall")));
        if (haveShell()) {
            int code = -1;
            runShell(QStringLiteral("/bin/sh -n -c ") + shellQuote(script), &code);
            QCOMPARE(code, 0); // parses as shell
            // The variables really hold the paths.
            const QString head = script.section(QStringLiteral("; "), 1, 2);
            QCOMPARE(runShell(head + QStringLiteral("; printf %s \"$S\"")), src);
        }
    }

    void uninstallScriptRemovesAndRestarts()
    {
        const QString script = macUninstallShellScript();
        QVERIFY(script.contains(QLatin1String("/bin/rm -rf '/Library/Audio/Plug-Ins/HAL/VocalInkVirtualMic.driver'")));
        QVERIFY(script.contains(QLatin1String("/usr/bin/killall coreaudiod")));
        if (haveShell()) {
            int code = -1;
            runShell(QStringLiteral("/bin/sh -n -c ") + shellQuote(script), &code);
            QCOMPARE(code, 0);
        }
    }

    void osascriptGetsOneAdminCommand()
    {
        const QString script = macInstallShellScript(QStringLiteral("/Apps/Vocal \"Ink\".app/X.driver"));
        const QString prompt = QStringLiteral("Vocal Ink needs your password to install its virtual microphone.");
        const QStringList args = osascriptArguments(script, prompt);
        QCOMPARE(args.size(), 2);
        QCOMPARE(args[0], QStringLiteral("-e"));
        const QString source = args[1];
        const QString head = QStringLiteral("do shell script \"");
        const QString tail = QStringLiteral("\" with prompt \"") + prompt
                             + QStringLiteral("\" with administrator privileges");
        QVERIFY(source.startsWith(head));
        QVERIFY(source.endsWith(tail));
        const QString literal = source.mid(head.size(), source.size() - head.size() - tail.size());
        QCOMPARE(appleScriptUnescape(literal), script);
    }

    void recognisesACancelledPasswordPrompt()
    {
        QVERIFY(osascriptWasCancelled(1, QStringLiteral("0:115: execution error: User canceled. (-128)")));
        QVERIFY(!osascriptWasCancelled(1, QStringLiteral("0:115: execution error: ditto: No such file (1)")));
        QVERIFY(!osascriptWasCancelled(0, QString()));
    }

    void readsTheBundleVersion()
    {
        // (No raw string literal here: moc 6.4 can't parse them.)
        const QByteArray plist =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "    <key>CFBundleShortVersionString</key>\n"
            "    <string>9.9</string>\n"
            "    <key>CFPlugInFactories</key>\n"
            "    <dict>\n"
            "        <key>0E3C7A62-3BBA-4C8A-9D8F-0C8F2F6D3B11</key>\n"
            "        <string>VocalInkVirtualMicEntryPoint</string>\n"
            "    </dict>\n"
            "    <key>CFBundleVersion</key>\n"
            "    <string> 1.2.3 </string>\n"
            "</dict>\n"
            "</plist>\n";
        QCOMPARE(plistBundleVersion(plist), QStringLiteral("1.2.3"));
        QCOMPARE(plistBundleVersion(QByteArray("<plist><dict></dict></plist>")), QString());
        QCOMPARE(plistBundleVersion(QByteArray("not xml")), QString());
    }

    void comparesVersions()
    {
        QVERIFY(compareVersions(QStringLiteral("1.0.1"), QStringLiteral("1.0.0")) > 0);
        QVERIFY(compareVersions(QStringLiteral("1.10"), QStringLiteral("1.9")) > 0);
        QVERIFY(compareVersions(QStringLiteral("1.0"), QStringLiteral("1.0.0")) == 0);
        QVERIFY(compareVersions(QStringLiteral("0.9"), QStringLiteral("1")) < 0);
        QVERIFY(compareVersions(QStringLiteral("2"), QString()) > 0);
    }

    // --- Windows ---------------------------------------------------------------------

    void nefconArguments()
    {
        const QString inf = QStringLiteral("C:/Program Files/Vocal Ink/driver/VocalInkAudio.inf");
        const QStringList install = nefconInstallArguments(inf);
        QCOMPARE(install.size(), 4);
        QCOMPARE(install[0], QStringLiteral("install"));
        QCOMPARE(install[1], QDir::toNativeSeparators(inf));
        QCOMPARE(install[2], QStringLiteral("ROOT\\VocalInkAudio"));
        QCOMPARE(install[3], QStringLiteral("--no-duplicates"));

        QCOMPARE(nefconUninstallArguments(),
                 QStringList({QStringLiteral("--remove-device-node"), QStringLiteral("--hardware-id"),
                              QStringLiteral("ROOT\\VocalInkAudio"), QStringLiteral("--class-guid"),
                              QStringLiteral("4d36e96c-e325-11ce-bfc1-08002be10318")}));
    }

    void windowsCommandLineQuoting()
    {
        QCOMPARE(windowsCommandLine({QStringLiteral("install"),
                                     QStringLiteral("C:\\Program Files\\Vocal Ink\\driver\\VocalInkAudio.inf"),
                                     QStringLiteral("ROOT\\VocalInkAudio"), QStringLiteral("--no-duplicates")}),
                 QStringLiteral("install \"C:\\Program Files\\Vocal Ink\\driver\\VocalInkAudio.inf\" "
                                "ROOT\\VocalInkAudio --no-duplicates"));
        QCOMPARE(windowsCommandLine({QStringLiteral("C:\\My Dir\\")}), QStringLiteral("\"C:\\My Dir\\\\\""));
        QCOMPARE(windowsCommandLine({QStringLiteral("a\"b")}), QStringLiteral("\"a\\\"b\""));
        QCOMPARE(windowsCommandLine({QStringLiteral("a\\\"b c")}), QStringLiteral("\"a\\\\\\\"b c\""));
        QCOMPARE(windowsCommandLine({QString()}), QStringLiteral("\"\""));
        QCOMPARE(windowsCommandLine(nefconUninstallArguments()),
                 QStringLiteral("--remove-device-node --hardware-id ROOT\\VocalInkAudio "
                                "--class-guid 4d36e96c-e325-11ce-bfc1-08002be10318"));
    }

    void windowsCommandLineRoundTrips()
    {
        const QString alphabet = QStringLiteral("ab \\\"\t'x");
        auto *rng = QRandomGenerator::global();
        for (int round = 0; round < 500; ++round) {
            QStringList args;
            const int count = rng->bounded(1, 5);
            for (int i = 0; i < count; ++i) {
                QString arg;
                const int len = rng->bounded(0, 8);
                for (int j = 0; j < len; ++j)
                    arg += alphabet[rng->bounded(int(alphabet.size()))];
                args << arg;
            }
            const QString line = windowsCommandLine(args);
            QCOMPARE(parseWindowsCommandLine(line), args);
        }
    }

    // --- State -------------------------------------------------------------------------

    void linuxStates()
    {
        Facts f;
        f.platform = Platform::Linux;
        QCOMPARE(describe(f).state, State::Unsupported);
        f.available = true;
        QCOMPARE(describe(f).state, State::NotInstalled);
        f.devicesVisible = true; // made with the old on-the-fly button: works until log-out
        QCOMPARE(describe(f).state, State::NotInstalled);
        QVERIFY(describe(f).text.contains(QLatin1String("until you log out")));
        f.installed = true;
        QCOMPARE(describe(f).state, State::Installed);
        QVERIFY(describe(f).text.contains(QStringLiteral("“Vocal Ink Mic”")));
        f.devicesVisible = false;
        QCOMPARE(describe(f).state, State::NotInstalled); // config but no devices: offer to fix it
        f.restartPending = true;
        QCOMPARE(describe(f).state, State::RestartNeeded);
        QVERIFY(describe(f).text.contains(QLatin1String("Log out")));
    }

    void macStates()
    {
        Facts f;
        f.platform = Platform::MacOS;
        QCOMPARE(describe(f).state, State::Unavailable);
        QVERIFY(describe(f).text.contains(QLatin1String("BlackHole")));
        f.available = true;
        QCOMPARE(describe(f).state, State::NotInstalled);
        f.installed = true;
        QCOMPARE(describe(f).state, State::RestartNeeded);
        QCOMPARE(describe(f).text, QStringLiteral("Your Mac needs a restart before the virtual mic appears."));
        f.devicesVisible = true;
        QCOMPARE(describe(f).state, State::Installed);
        QVERIFY(describe(f).text.contains(QStringLiteral("“Vocal Ink Virtual Mic”")));
        f.updateAvailable = true;
        QCOMPARE(describe(f).state, State::NotInstalled);
        QVERIFY(describe(f).text.contains(QLatin1String("An update to the virtual mic is available.")));
        f.available = false; // an installed driver keeps working in a build without one
        QCOMPARE(describe(f).state, State::Installed);
    }

    void windowsStates()
    {
        Facts f;
        f.platform = Platform::Windows;
        QCOMPARE(describe(f).state, State::Unavailable);
        QCOMPARE(describe(f).text,
                 QStringLiteral("This build doesn't include the signed Windows driver. Use VB-CABLE instead (free)."));
        f.available = true;
        QCOMPARE(describe(f).state, State::NotInstalled);
        f.restartPending = true;
        QCOMPARE(describe(f).state, State::RestartNeeded);
        f.restartPending = false;
        f.installed = true;
        QCOMPARE(describe(f).state, State::RestartNeeded);
        f.devicesVisible = true;
        QCOMPARE(describe(f).state, State::Installed);
        QVERIFY(describe(f).text.contains(QStringLiteral("“Vocal Ink Mic”")));
        f.available = false; // installed some other way: still usable
        QCOMPARE(describe(f).state, State::Installed);
    }

    void otherSystemsAreUnsupported()
    {
        QCOMPARE(describe(Facts()).state, State::Unsupported);
    }

    void deviceNamesPerPlatform()
    {
        QCOMPARE(outputNameFor(Platform::Windows), QStringLiteral("Vocal Ink Voice"));
        QCOMPARE(inputNameFor(Platform::Windows), QStringLiteral("Vocal Ink Mic"));
        QCOMPARE(outputNameFor(Platform::Linux), QStringLiteral("Vocal Ink Voice"));
        QCOMPARE(inputNameFor(Platform::Linux), QStringLiteral("Vocal Ink Mic"));
        QCOMPARE(outputNameFor(Platform::MacOS), QStringLiteral("Vocal Ink Virtual Mic"));
        QCOMPARE(inputNameFor(Platform::MacOS), QStringLiteral("Vocal Ink Virtual Mic"));
    }

    // --- The object --------------------------------------------------------------------

    void objectReportsNamesAndAState()
    {
        VirtualDriver driver;
        QCOMPARE(driver.displayName(), QStringLiteral("Vocal Ink Virtual Mic"));
        QCOMPARE(driver.outputDeviceName(), outputNameFor(currentPlatform()));
        QCOMPARE(driver.inputDeviceName(), inputNameFor(currentPlatform()));
        QVERIFY(!driver.busy());
        QVERIFY(!driver.statusText().isEmpty());
#if defined(Q_OS_LINUX)
        QVERIFY(!driver.needsAdmin());
        QVERIFY(driver.state() != State::Unavailable);
        QVERIFY(driver.state() != State::Failed);
#elif defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QVERIFY(driver.needsAdmin());
#endif
    }

    // Needs a running PulseAudio/PipeWire server; skipped elsewhere (e.g. CI).
    void linuxInstallAndUninstall()
    {
#if !defined(Q_OS_LINUX)
        QSKIP("Linux only");
#else
        if (!pulseServerRunning())
            QSKIP("no PulseAudio/PipeWire server");
        // Keep the real ~/.config out of it. (Changing XDG_CONFIG_HOME instead would also
        // move where pactl looks for the server.)
        QStandardPaths::setTestModeEnabled(true);
        const auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
        const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QVERIFY(configHome.contains(QLatin1String("qttest")));
        QFile::remove(pipeWireConfPath(configHome));
        QFile::remove(pulseDefaultPaPath(configHome));
        VirtualDriver driver;
        QSignalSpy finished(&driver, &VirtualDriver::finished);
        driver.install();
        QVERIFY(driver.busy());
        QVERIFY(finished.wait(30000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QVERIFY(!driver.busy());
        const bool pipeWire = QFile::exists(pipeWireConfPath(configHome));
        const bool pulse = QFile::exists(pulseDefaultPaPath(configHome));
        QVERIFY(pipeWire || pulse);
        QCOMPARE(driver.state(), State::Installed);
        QVERIFY(!driver.outputDeviceId().isEmpty());

        finished.clear();
        driver.uninstall();
        QVERIFY(finished.wait(30000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QVERIFY(!QFile::exists(pipeWireConfPath(configHome)));
        QVERIFY(!QFile::exists(pulseDefaultPaPath(configHome))); // we created it, so it goes away
        QCOMPARE(driver.state(), State::NotInstalled);
#endif
    }
};

QTEST_GUILESS_MAIN(TestVirtualDriver)
#include "test_virtualdriver.moc"
