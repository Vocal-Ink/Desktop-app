#include "Version.h"
#include "app/AppContext.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(QStringLiteral(VOCALINK_ORG_NAME));
    QApplication::setApplicationName(QStringLiteral(VOCALINK_DISPLAY_NAME));
    QApplication::setApplicationVersion(QStringLiteral(VOCALINK_VERSION));
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Speak-for-me text-to-speech"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save a screenshot of the main window to <file> and quit."),
                                  QStringLiteral("file"));
    parser.addOption(screenshot);
    parser.process(app);

    AppContext context;
    context.initialize();

    MainWindow window(&context);
    window.show();

    if (parser.isSet(screenshot)) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(1500, &window, [&window, path] {
            window.grab().save(path);
            QApplication::quit();
        });
    }
    return app.exec();
}
