#include "audio/Soundboard.h"

// Placeholder implementation (replaced by the audio work package).
class Soundboard::Private {};

Soundboard::Soundboard(AudioPlayer *player, const QString &storeDir, QObject *parent)
    : QObject(parent), d(new Private), m_player(player), m_dir(storeDir) {}
Soundboard::~Soundboard() { delete d; }
bool Soundboard::load() { return true; }
bool Soundboard::save() const { return true; }
int Soundboard::indexOf(const QString &id) const
{
    for (int i = 0; i < m_sounds.size(); ++i) {
        if (m_sounds.at(i).id == id)
            return i;
    }
    return -1;
}
QString Soundboard::addFile(const QString &, QString *error)
{
    if (error)
        *error = tr("The soundboard is not available in this build.");
    return {};
}
void Soundboard::update(const Sound &) {}
void Soundboard::remove(const QString &) {}
void Soundboard::move(int, int) {}
void Soundboard::play(const QString &) {}
void Soundboard::stop(const QString &) {}
void Soundboard::stopAll() {}
bool Soundboard::isPlaying(const QString &) const { return false; }
QStringList Soundboard::supportedExtensions()
{
    return {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("flac"),
            QStringLiteral("m4a"), QStringLiteral("opus")};
}
