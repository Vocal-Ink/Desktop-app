#include "audio/MicPassthrough.h"

// Placeholder implementation (replaced by the audio work package).
class MicPassthrough::Private {};

MicPassthrough::MicPassthrough(AudioPlayer *player, QObject *parent)
    : QObject(parent), d(new Private), m_player(player) {}
MicPassthrough::~MicPassthrough() { delete d; }
void MicPassthrough::setMode(Mode mode) { m_mode = mode; setLive(mode == Mode::AlwaysOn); }
void MicPassthrough::setInputDevice(const QByteArray &) {}
void MicPassthrough::setGainDb(float) {}
void MicPassthrough::setGateDb(float) {}
void MicPassthrough::setDuckDuringSpeech(bool, float) {}
void MicPassthrough::press() { if (m_mode == Mode::HoldKey) setLive(true); }
void MicPassthrough::release() { if (m_mode == Mode::HoldKey) setLive(false); }
void MicPassthrough::toggle() { if (m_mode != Mode::Off) setLive(!m_live); }
void MicPassthrough::setLive(bool live)
{
    if (m_live == live)
        return;
    m_live = live;
    emit liveChanged(live);
}
MicPassthrough::Mode MicPassthrough::modeFromString(const QString &s)
{
    if (s == QLatin1String("hold"))
        return Mode::HoldKey;
    if (s == QLatin1String("toggle"))
        return Mode::ToggleKey;
    if (s == QLatin1String("always"))
        return Mode::AlwaysOn;
    return Mode::Off;
}
QString MicPassthrough::modeToString(Mode m)
{
    switch (m) {
    case Mode::HoldKey: return QStringLiteral("hold");
    case Mode::ToggleKey: return QStringLiteral("toggle");
    case Mode::AlwaysOn: return QStringLiteral("always");
    case Mode::Off: break;
    }
    return QStringLiteral("off");
}
