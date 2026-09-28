#pragma once

#include <QString>
#include <QStringList>

class Settings;

// Backs up and restores preferences, quick phrases, voice presets, the
// soundboard list and learned words as one JSON file. API keys are never
// exported.
namespace SettingsTransfer {

bool exportTo(const QString &filePath, Settings *settings, QString *error = nullptr);
// Returns false (and leaves everything untouched) if the file isn't a valid backup.
// Stores that were already loaded (phrases, presets, sounds, words) need to be
// reloaded afterwards.
bool importFrom(const QString &filePath, Settings *settings, QString *error = nullptr);

// The JSON files in Paths::dataDir() that are part of a backup.
QStringList dataFiles();

} // namespace SettingsTransfer
