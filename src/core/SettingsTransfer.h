#pragma once

#include <QString>

class Settings;

// Backs up and restores preferences, quick phrases, voice presets, the
// soundboard list and learned words as one JSON file. API keys are never
// exported.
namespace SettingsTransfer {

bool exportTo(const QString &filePath, Settings *settings, QString *error = nullptr);
// Returns false (and leaves everything untouched) if the file isn't a valid backup.
bool importFrom(const QString &filePath, Settings *settings, QString *error = nullptr);

} // namespace SettingsTransfer
