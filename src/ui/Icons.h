#pragma once

#include <QColor>
#include <QIcon>

// Small vector icons drawn at runtime so they follow the theme and never
// depend on emoji fonts being installed.
namespace Icons {

QIcon mic(const QColor &color);
QIcon stop(const QColor &color);
QIcon speak(const QColor &color);
QIcon gear(const QColor &color);

} // namespace Icons
