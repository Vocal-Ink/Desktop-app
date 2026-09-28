#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class AudioPlayer;

struct Sound
{
    QString id;       // stable identifier (also the file base name in the store)
    QString name;     // shown on the button
    QString file;     // absolute path of the copy inside the store
    QString hotkey;   // optional global shortcut (QKeySequence portable text)
    float gain = 1.0f;
    QString color;    // optional button tint, "#rrggbb"
};

// Sound effects the user can fire into the voice output (applause, a laugh,
// "bruh"...). Files are copied into the app's data folder, decoded once and
// played through AudioPlayer::playSound so they mix with speech.
class Soundboard : public QObject
{
    Q_OBJECT
public:
    Soundboard(AudioPlayer *player, const QString &storeDir, QObject *parent = nullptr);
    ~Soundboard() override;

    bool load();
    bool save() const;

    const QList<Sound> &sounds() const { return m_sounds; }
    int indexOf(const QString &id) const;
    // Copies an audio file (wav, mp3, ogg, flac, m4a...) into the store. Returns the new id.
    QString addFile(const QString &path, QString *error = nullptr);
    void update(const Sound &sound);
    void remove(const QString &id);
    void move(int from, int to);

    void play(const QString &id);
    void stop(const QString &id);
    void stopAll();
    bool isPlaying(const QString &id) const;

    static QStringList supportedExtensions();

signals:
    void changed();
    void playingChanged(const QString &id, bool playing);
    void errorOccurred(const QString &message);

private:
    class Private;
    Private *d;
    AudioPlayer *m_player;
    QString m_dir;
    QList<Sound> m_sounds;
};
