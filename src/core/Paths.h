#pragma once

#include <QString>

// Locations of user data. Everything lives under the per-user app data
// directory unless the app runs in portable mode (a "portable.txt" file next to
// the executable), in which case it lives next to the executable.
namespace Paths {

QString dataDir();          // root for downloaded models, phrases, history
QString whisperModelsDir(); // ggml-*.bin speech recognition models
QString piperRuntimeDir();  // extracted piper binary release
QString piperVoicesDir();   // <key>/<key>.onnx + .onnx.json
QString phrasesFile();
QString settingsFile();     // only used in portable mode
bool isPortable();

// Test hook: redirect dataDir() to a temporary location.
void setDataDirOverride(const QString &dir);

QString ensureDir(const QString &path);

} // namespace Paths
