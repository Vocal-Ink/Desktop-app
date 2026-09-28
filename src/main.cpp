#include "Version.h"
#include "app/AppContext.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {

QString instanceKey()
{
    // One instance per user: hotkeys and the overlay port can't be shared.
    return QStringLiteral("vocalink-%1").arg(QDir::home().dirName());
}

// Returns true if another instance is already running (and asks it to show itself).
bool signalRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(instanceKey());
    if (!socket.waitForConnected(300))
        return false;
    socket.write("show\n");
    socket.waitForBytesWritten(300);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral(VOCALINK_ORG_NAME));
    QApplication::setApplicationName(QStringLiteral(VOCALINK_DISPLAY_NAME));
    QApplication::setApplicationVersion(QStringLiteral(VOCALINK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.vocalink.desktop"));
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    app.setQuitOnLastWindowClosed(false); // the tray keeps shortcuts alive

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Speak-for-me text-to-speech"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot of the main window to <file> and quit."),
                                        QStringLiteral("file"));
    const QCommandLineOption noWizard(QStringLiteral("no-wizard"), QStringLiteral("Don't open the setup assistant."));
    const QCommandLineOption minimized(QStringLiteral("minimized"), QStringLiteral("Start in the system tray."));
    const QCommandLineOption showWhat(QStringLiteral("show"),
                                      QStringLiteral("Open a window at start: wizard, voices or settings[:page]."),
                                      QStringLiteral("what"));
    parser.addOption(screenshot);
    parser.addOption(showWhat);
    parser.addOption(noWizard);
    parser.addOption(minimized);
    parser.process(app);

    const bool smokeTest = parser.isSet(screenshot);
    if (!smokeTest && signalRunningInstance())
        return 0;

    QLocalServer instanceServer;
    if (!smokeTest) {
        QLocalServer::removeServer(instanceKey());
        instanceServer.listen(instanceKey());
    }

    AppContext context;
    context.initialize();

    MainWindow window(&context);
    window.setFirstRunWizardEnabled(!smokeTest && !parser.isSet(noWizard));
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&] {
        while (QLocalSocket *s = instanceServer.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &context] {
                if (s->readAll().startsWith("show"))
                    emit context.showWindowRequested();
                s->deleteLater();
            });
        }
    });

    if (!parser.isSet(minimized))
        window.show();

    if (parser.isSet(showWhat)) {
        const QString what = parser.value(showWhat);
        QTimer::singleShot(300, &window, [&window, what] {
            if (what == QLatin1String("wizard"))
                window.runSetupWizard();
            else if (what == QLatin1String("voices"))
                window.openVoicePicker();
            else if (what.startsWith(QLatin1String("settings")))
                window.showSettings(what.section(QLatin1Char(':'), 1));
        });
    }

    if (smokeTest) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(2500, &window, [&window, path] {
            // Capture the front-most window (a dialog opened with --show, or the main window).
            QWidget *target = QApplication::activeModalWidget();
            if (!target) {
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *w : tops) {
                    if (w->isVisible() && w != &window && w->inherits("QDialog"))
                        target = w;
                }
            }
            if (!target)
                target = &window;
            const bool ok = target->grab().save(path);
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
