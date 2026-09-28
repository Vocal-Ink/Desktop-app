#pragma once

#include <QString>

class QApplication;

namespace Theme {

// "dark" (default), "light" or "contrast" (high contrast, larger focus rings).
void apply(QApplication *app, const QString &name, int fontScalePercent);

// Accent colour used for the primary "Speak" action and highlights.
QString accent();

} // namespace Theme
