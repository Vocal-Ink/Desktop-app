#include "Version.h"
#include "app/AppContext.h"
#include "audio/MicPassthrough.h"
#include "core/HistoryModel.h"
#include "core/Settings.h"
#include "platform/VirtualDriver.h"
#include "ui/Bridge.h"
#include "ui/InkWave.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QSystemTrayIcon>
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

void loadFonts()
{
    const QDir dir(QStringLiteral(":/fonts"));
    const QStringList files = dir.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
    for (const QString &file : files)
        QFontDatabase::addApplicationFont(dir.filePath(file));
}

QSystemTrayIcon *createTray(Bridge *bridge, AppContext *context, QObject *parent)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return nullptr;
    auto *tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/tray.png")), parent);
    tray->setToolTip(QStringLiteral(VOCALINK_DISPLAY_NAME));
    auto *menu = new QMenu;
    QObject::connect(tray, &QObject::destroyed, menu, &QObject::deleteLater);
    menu->addAction(QObject::tr("Open Vocal Ink"), bridge, [bridge] { emit bridge->uiAction(QStringLiteral("window.show")); });
    menu->addAction(QObject::tr("Quick type…"), bridge, [bridge] { emit bridge->uiAction(QStringLiteral("window.quickType")); });
    menu->addSeparator();
    menu->addAction(QObject::tr("Stop speaking"), context, &AppContext::stopSpeaking);
    QAction *mute = menu->addAction(QObject::tr("Mute my real mic"), context, [context] { context->mic()->setLive(false); });
    mute->setEnabled(false);
    QObject::connect(context->mic(), &MicPassthrough::liveChanged, mute, &QAction::setEnabled);
    menu->addSeparator();
    menu->addAction(QObject::tr("Quit"), qApp, &QCoreApplication::quit);
    tray->setContextMenu(menu);
    QObject::connect(tray, &QSystemTrayIcon::activated, bridge, [bridge](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit bridge->uiAction(QStringLiteral("window.toggle"));
    });
    tray->show();
    return tray;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral(VOCALINK_ORG_NAME));
    QApplication::setApplicationName(QStringLiteral(VOCALINK_DISPLAY_NAME));
    QApplication::setApplicationVersion(QStringLiteral(VOCALINK_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("org.vocalink.desktop"));

    // Smooth edges on the ink strokes without paying for MSAA everywhere.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-256.png")));
    app.setQuitOnLastWindowClosed(false); // the tray keeps shortcuts alive
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Speak-for-me text-to-speech"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a screenshot of the main window to <file> and quit."),
                                        QStringLiteral("file"));
    const QCommandLineOption minimized(QStringLiteral("minimized"), QStringLiteral("Start in the system tray."));
    const QCommandLineOption showWhat(QStringLiteral("show"),
                                      QStringLiteral("Open a page at start, e.g. voices, settings:shortcuts, onboarding:3."),
                                      QStringLiteral("what"));
    const QCommandLineOption demo(QStringLiteral("demo"), QStringLiteral("Fill the screen with sample content (screenshots)."));
    parser.addOption(screenshot);
    parser.addOption(showWhat);
    parser.addOption(minimized);
    parser.addOption(demo);
    parser.process(app);

    const bool smokeTest = parser.isSet(screenshot);
    if (!smokeTest && signalRunningInstance())
        return 0;

    QLocalServer instanceServer;
    if (!smokeTest) {
        QLocalServer::removeServer(instanceKey());
        instanceServer.listen(instanceKey());
    }

    loadFonts();

    AppContext context;
    context.initialize();
    Bridge bridge(&context);

    qmlRegisterSingletonInstance("Ink.Core", 1, 0, "App", &bridge);
    qmlRegisterType<InkWave>("Ink.Core", 1, 0, "InkWave");
    qmlRegisterUncreatableType<VirtualDriver>("Ink.Core", 1, 0, "VirtualDriver", QStringLiteral("Use App.virtualMic"));
    qmlRegisterUncreatableType<HistoryModel>("Ink.Core", 1, 0, "HistoryModel", QStringLiteral("Use App.history"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("launchPage"), parser.value(showWhat));
    engine.rootContext()->setContextProperty(QStringLiteral("launchMinimized"), parser.isSet(minimized));
    engine.rootContext()->setContextProperty(QStringLiteral("demoMode"), parser.isSet(demo));
    engine.rootContext()->setContextProperty(QStringLiteral("smokeTest"), smokeTest);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Ink", "Main");
#else
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Ink/Main.qml")));
#endif
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    bridge.setMainWindow(window);
    if (parser.isSet(demo))
        QTimer::singleShot(200, &bridge, &Bridge::startDemo);

    QSystemTrayIcon *tray = smokeTest ? nullptr : createTray(&bridge, &context, &app);
    Q_UNUSED(tray)

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &bridge, [&] {
        while (QLocalSocket *s = instanceServer.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &bridge] {
                if (s->readAll().startsWith("show"))
                    emit bridge.uiAction(QStringLiteral("window.show"));
                s->deleteLater();
            });
        }
    });

    if (smokeTest && window) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(3000, window, [window, path] {
            const bool ok = window->grabWindow().save(path);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
