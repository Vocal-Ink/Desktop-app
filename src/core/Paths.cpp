#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString &overrideDir()
{
    static QString dir;
    return dir;
}

bool &profileMode()
{
    static bool on = false;
    return on;
}
} // namespace

namespace Paths {

bool isPortable()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return !appDir.isEmpty() && QFileInfo::exists(appDir + QStringLiteral("/portable.txt"));
}

QString ensureDir(const QString &path)
{
    QDir().mkpath(path);
    return path;
}

void setDataDirOverride(const QString &dir)
{
    overrideDir() = dir;
}

void setProfileDir(const QString &dir)
{
    overrideDir() = dir;
    profileMode() = !dir.isEmpty();
}

bool usesIniSettings()
{
    return profileMode() || isPortable();
}

QString dataDir()
{
    if (!overrideDir().isEmpty())
        return ensureDir(overrideDir());
    if (isPortable())
        return ensureDir(QCoreApplication::applicationDirPath() + QStringLiteral("/data"));
    return ensureDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

QString whisperModelsDir()
{
    return ensureDir(dataDir() + QStringLiteral("/models/whisper"));
}

QString piperRuntimeDir()
{
    return ensureDir(dataDir() + QStringLiteral("/piper"));
}

QString piperVoicesDir()
{
    return ensureDir(dataDir() + QStringLiteral("/models/piper"));
}

QString phrasesFile()
{
    return dataDir() + QStringLiteral("/phrases.json");
}

QString settingsFile()
{
    return dataDir() + QStringLiteral("/settings.ini");
}

} // namespace Paths
