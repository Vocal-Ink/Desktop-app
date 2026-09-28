#include "core/SecretStore.h"

#include "core/Paths.h"

#include <QSettings>
#include <QTimer>
#include <memory>

#ifdef VOCALINK_HAVE_KEYCHAIN
#include <qtkeychain/keychain.h>
#endif

namespace {
const QString kService = QStringLiteral("Vocal Ink");
const QString kFallbackGroup = QStringLiteral("secrets-fallback/");

// Used when the OS password manager is unreachable (e.g. no Secret Service
// running on a minimal Linux desktop). Values are stored in the user's settings.
std::unique_ptr<QSettings> fallbackSettings()
{
    return Paths::isPortable() ? std::make_unique<QSettings>(Paths::settingsFile(), QSettings::IniFormat)
                               : std::make_unique<QSettings>();
}

QString readFallback(const QString &name)
{
    return fallbackSettings()->value(kFallbackGroup + name).toString();
}

void writeFallback(const QString &name, const QString &value)
{
    auto settings = fallbackSettings();
    QSettings &s = *settings;
    if (value.isEmpty())
        s.remove(kFallbackGroup + name);
    else
        s.setValue(kFallbackGroup + name, value);
}
} // namespace

QStringList Secrets::all()
{
    return {Azure, ElevenLabs, FishAudio, OpenAi, OpenAiStt, Obs, VTubeStudio};
}

SecretStore::SecretStore(Backend backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
#ifndef VOCALINK_HAVE_KEYCHAIN
    m_backend = Backend::Memory;
#endif
}

void SecretStore::load()
{
    if (m_backend == Backend::Memory) {
        m_loaded = true;
        QTimer::singleShot(0, this, &SecretStore::loaded);
        return;
    }
    m_pending = Secrets::all();
    readNext();
}

void SecretStore::useFallback()
{
    if (m_keychainBroken)
        return;
    m_keychainBroken = true;
    emit errorOccurred(tr("Your system's password manager isn't available, so API keys are saved in "
                          "Vocal Ink's settings file instead."));
}

void SecretStore::readNext()
{
#ifdef VOCALINK_HAVE_KEYCHAIN
    if (m_pending.isEmpty()) {
        m_loaded = true;
        emit loaded();
        return;
    }
    const QString name = m_pending.takeFirst();
    if (m_keychainBroken) {
        const QString value = readFallback(name);
        if (!value.isEmpty())
            m_cache.insert(name, value);
        readNext();
        return;
    }
    auto *job = new QKeychain::ReadPasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setInsecureFallback(true);
    job->setKey(name);
    connect(job, &QKeychain::Job::finished, this, [this, job, name] {
        if (job->error() == QKeychain::NoError) {
            m_cache.insert(name, job->textData());
        } else if (job->error() != QKeychain::EntryNotFound) {
            useFallback();
            const QString value = readFallback(name);
            if (!value.isEmpty())
                m_cache.insert(name, value);
        } else {
            // Keys saved while the keychain was unavailable.
            const QString value = readFallback(name);
            if (!value.isEmpty())
                m_cache.insert(name, value);
        }
        readNext();
    });
    job->start();
#endif
}

void SecretStore::set(const QString &name, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (m_cache.value(name) == trimmed)
        return;
    if (trimmed.isEmpty())
        m_cache.remove(name);
    else
        m_cache.insert(name, trimmed);
    emit changed(name);

#ifdef VOCALINK_HAVE_KEYCHAIN
    if (m_backend != Backend::Keychain)
        return;
    if (m_keychainBroken) {
        writeFallback(name, trimmed);
        return;
    }
    QKeychain::Job *job = nullptr;
    if (trimmed.isEmpty()) {
        auto *del = new QKeychain::DeletePasswordJob(kService, this);
        del->setKey(name);
        job = del;
    } else {
        auto *write = new QKeychain::WritePasswordJob(kService, this);
        write->setKey(name);
        write->setTextData(trimmed);
        job = write;
    }
    job->setAutoDelete(true);
    job->setInsecureFallback(true);
    connect(job, &QKeychain::Job::finished, this, [this, job, name, trimmed] {
        if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
            writeFallback(name, QString()); // don't leave a stale plain-text copy behind
            return;
        }
        useFallback();
        writeFallback(name, trimmed);
    });
    job->start();
#endif
}
