#pragma once

#include "tts/Voice.h"

#include <QJsonDocument>
#include <QList>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;
class QObject;
class Settings;

// Plumbing shared by the cloud engines (Azure, ElevenLabs, Fish Audio, OpenAI).
namespace CloudTts {

using JsonHandler = std::function<void(const QJsonDocument &json, const QString &error)>;

// GETs `request` and calls `done` with the parsed JSON, or with a readable
// error (empty JSON). Nothing is called once `context` is destroyed.
void getJson(QNetworkAccessManager *network, const QNetworkRequest &request, const QString &provider,
             QObject *context, JsonHandler done);

// Voice ids the user pasted from a provider's website, stored as a QStringList
// of "id|name" under `settingsKey`.
QList<Voice> customVoices(const Settings *settings, const char *settingsKey, const QString &engineId);
void addCustomVoice(Settings *settings, const char *settingsKey, const QString &id, const QString &name);
void removeCustomVoice(Settings *settings, const char *settingsKey, const QString &id);

// Appends the voices of `extra` whose id is not in `list` yet.
void mergeVoices(QList<Voice> &list, const QList<Voice> &extra);

// "female" -> "Female", "male" -> "Male", anything else -> "".
QString normalizeGender(const QString &gender);

} // namespace CloudTts
