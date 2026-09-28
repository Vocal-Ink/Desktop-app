#pragma once

#include <QByteArray>
#include <QObject>

// Short, soft sound cues played only to the user's own speakers/headphones
// (never into the virtual cable): helpful when you can't watch the screen.
class Earcons : public QObject
{
    Q_OBJECT
public:
    enum class Cue { ListenStart, ListenStop, Sent, Error, MicLive, MicMuted, Notify };
    Q_ENUM(Cue)

    explicit Earcons(QObject *parent = nullptr);
    ~Earcons() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setDevice(const QByteArray &deviceId); // empty = system default output
    void setVolume(float volume);               // 0..1

    void play(Cue cue);

private:
    class Private;
    Private *d;
    bool m_enabled = true;
};
