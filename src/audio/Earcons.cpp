#include "audio/Earcons.h"

// Placeholder implementation (replaced by the audio work package).
class Earcons::Private {};

Earcons::Earcons(QObject *parent) : QObject(parent), d(new Private) {}
Earcons::~Earcons() { delete d; }
void Earcons::setEnabled(bool enabled) { m_enabled = enabled; }
void Earcons::setDevice(const QByteArray &) {}
void Earcons::setVolume(float) {}
void Earcons::play(Cue) {}
