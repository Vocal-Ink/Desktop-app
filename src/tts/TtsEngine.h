#pragma once

#include "tts/Voice.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class Settings;
class SecretStore;

// Audio from one synthesis request. Emits audioReady() one or more times (as
// data streams in), then exactly one of finished() or failed(). After cancel()
// nothing more is emitted. The requester owns the stream (deleteLater()).
class TtsStream : public QObject
{
    Q_OBJECT
public:
    explicit TtsStream(QObject *parent = nullptr);

    void cancel();
    bool isCancelled() const { return m_cancelled; }
    bool isDone() const { return m_done; }

signals:
    void audioReady(const QAudioFormat &format, const QByteArray &pcm);
    void finished();
    void failed(const QString &error);

protected:
    virtual void onCancel() {}
    void deliverAudio(const QAudioFormat &format, const QByteArray &pcm);
    void deliverFinished();
    void deliverFailed(const QString &error);
    // Deferred variants so engines can return the stream before anything fires.
    void deliverFailedLater(const QString &error);

private:
    bool m_cancelled = false;
    bool m_done = false;
};

// Shared services handed to every engine.
struct EngineContext
{
    QNetworkAccessManager *network = nullptr;
    Settings *settings = nullptr;
    SecretStore *secrets = nullptr;
};

class TtsEngine : public QObject
{
    Q_OBJECT
public:
    explicit TtsEngine(const EngineContext &context, QObject *parent = nullptr);

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isLocal() const = 0;
    // Ready to synthesize (API key present, runtime installed, ...).
    virtual bool isAvailable() const = 0;
    // Human-readable explanation when !isAvailable(), e.g. "Add your ElevenLabs API key in Settings".
    virtual QString unavailableReason() const { return {}; }

    // Asynchronously (re)loads the voice list; emits voicesChanged() or voicesError().
    virtual void refreshVoices() = 0;
    QList<Voice> voices() const { return m_voices; }
    bool isRefreshing() const { return m_refreshing; }

    virtual TtsStream *synthesize(const QString &text, const Voice &voice, const SpeakOptions &options) = 0;

    // Engines with a huge server-side catalogue (Fish Audio) can search it.
    virtual bool supportsRemoteSearch() const { return false; }
    virtual void searchVoices(const QString &query) { Q_UNUSED(query) }
    // Engines where the user may paste a voice id from the provider's website.
    virtual bool supportsCustomVoiceIds() const { return false; }
    virtual void addCustomVoice(const QString &id, const QString &name) { Q_UNUSED(id) Q_UNUSED(name) }
    virtual void removeCustomVoice(const QString &id) { Q_UNUSED(id) }

    // The base URL the engine talks to (tests point this at a local mock server).
    void setBaseUrlOverride(const QString &url) { m_baseUrlOverride = url; }

signals:
    void voicesChanged();
    void voicesError(const QString &message);
    void availabilityChanged();
    void searchFinished(const QString &query, const QList<Voice> &results);

protected:
    void setVoices(const QList<Voice> &voices);
    void setRefreshing(bool refreshing) { m_refreshing = refreshing; }
    QString baseUrl(const QString &defaultUrl) const
    {
        return m_baseUrlOverride.isEmpty() ? defaultUrl : m_baseUrlOverride;
    }

    EngineContext m_ctx;
    QList<Voice> m_voices;
    bool m_refreshing = false;
    QString m_baseUrlOverride;
};
