#include "core/SecretStore.h"

#include <QTimer>

#ifdef VOCALINK_HAVE_KEYCHAIN
#include <qtkeychain/keychain.h>
#endif

namespace {
const QString kService = QStringLiteral("Vocal Ink");
}

QStringList Secrets::all()
{
    return {Azure, ElevenLabs, FishAudio, OpenAi, OpenAiStt, Obs};
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

void SecretStore::readNext()
{
#ifdef VOCALINK_HAVE_KEYCHAIN
    if (m_pending.isEmpty()) {
        m_loaded = true;
        emit loaded();
        return;
    }
    const QString name = m_pending.takeFirst();
    auto *job = new QKeychain::ReadPasswordJob(kService, this);
    job->setAutoDelete(true);
    job->setInsecureFallback(true);
    job->setKey(name);
    connect(job, &QKeychain::Job::finished, this, [this, job, name] {
        if (job->error() == QKeychain::NoError) {
            m_cache.insert(name, job->textData());
        } else if (job->error() != QKeychain::EntryNotFound) {
            emit errorOccurred(tr("Could not read the saved %1 key: %2").arg(name, job->errorString()));
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
    connect(job, &QKeychain::Job::finished, this, [this, job, name] {
        if (job->error() != QKeychain::NoError && job->error() != QKeychain::EntryNotFound)
            emit errorOccurred(tr("Could not save the %1 key: %2").arg(name, job->errorString()));
    });
    job->start();
#endif
}
