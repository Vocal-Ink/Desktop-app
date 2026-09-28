#include "ui/LevelMeter.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(tr("Microphone level"));
}

void LevelMeter::setLevel(float level)
{
    // Fast attack, slow release so the bar is readable.
    const float target = qBound(0.0f, level, 1.0f);
    m_level = target > m_level ? target : m_level * 0.8f + target * 0.2f;
    update();
}

void LevelMeter::setActive(bool active)
{
    m_active = active;
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = r.height() / 2.0;
    QColor track = palette().color(QPalette::Mid);
    track.setAlpha(90);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(r, radius, radius);

    QRectF fill = r;
    fill.setWidth(r.width() * qreal(m_level));
    QColor c = m_active ? QColor(QStringLiteral("#3ddc97")) : QColor(Theme::accent());
    p.setBrush(c);
    p.drawRoundedRect(fill, radius, radius);
}
