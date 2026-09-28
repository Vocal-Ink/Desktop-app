#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

// Well-known secret names.
namespace Secrets {
inline const QString Azure = QStringLiteral("azure");
inline const QString ElevenLabs = QStringLiteral("elevenlabs");
inline const QString FishAudio = QStringLiteral("fishaudio");
inline const QString OpenAi = QStringLiteral("openai");
inline const QString OpenAiStt = QStringLiteral("openai-stt");
inline const QString Obs = QStringLiteral("obs");
QStringList all();
} // namespace Secrets

// API keys and passwords. Values are cached in memory after load(); the backing
// store is the OS keychain (Windows Credential Manager, macOS Keychain, Secret
// Service/KWallet) when available, otherwise QSettings ("insecure fallback").
class SecretStore : public QObject
{
    Q_OBJECT
public:
    enum class Backend { Keychain, Memory };

    explicit SecretStore(Backend backend = Backend::Keychain, QObject *parent = nullptr);

    // Asynchronously reads every secret in Secrets::all(); emits loaded() once done.
    void load();
    bool isLoaded() const { return m_loaded; }

    QString get(const QString &name) const { return m_cache.value(name); }
    bool has(const QString &name) const { return !m_cache.value(name).isEmpty(); }
    // Updates the cache immediately and persists asynchronously. Empty value deletes.
    void set(const QString &name, const QString &value);

signals:
    void loaded();
    void changed(const QString &name);
    void errorOccurred(const QString &message);

private:
    void readNext();
    void useFallback();

    Backend m_backend;
    bool m_loaded = false;
    bool m_keychainBroken = false;
    QStringList m_pending;
    QHash<QString, QString> m_cache;
};
