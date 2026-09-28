#include "ui/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>
#include <functional>

namespace Icons {

namespace {

QIcon draw(const QColor &color, const std::function<void(QPainter &, qreal)> &paint)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, qMax(1.5, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        paint(p, size);
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

QIcon mic(const QColor &color)
{
    return draw(color, [color](QPainter &p, qreal s) {
        p.setBrush(color);
        p.drawRoundedRect(QRectF(s * 0.36, s * 0.08, s * 0.28, s * 0.5), s * 0.14, s * 0.14);
        p.setBrush(Qt::NoBrush);
        QPainterPath arc;
        arc.moveTo(s * 0.22, s * 0.42);
        arc.cubicTo(s * 0.22, s * 0.78, s * 0.78, s * 0.78, s * 0.78, s * 0.42);
        p.drawPath(arc);
        p.drawLine(QPointF(s * 0.5, s * 0.72), QPointF(s * 0.5, s * 0.88));
        p.drawLine(QPointF(s * 0.34, s * 0.9), QPointF(s * 0.66, s * 0.9));
    });
}

QIcon stop(const QColor &color)
{
    return draw(color, [color](QPainter &p, qreal s) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(s * 0.24, s * 0.24, s * 0.52, s * 0.52), s * 0.08, s * 0.08);
    });
}

QIcon speak(const QColor &color)
{
    return draw(color, [color](QPainter &p, qreal s) {
        // Speaker cone + two sound waves.
        QPainterPath cone;
        cone.moveTo(s * 0.12, s * 0.38);
        cone.lineTo(s * 0.28, s * 0.38);
        cone.lineTo(s * 0.48, s * 0.2);
        cone.lineTo(s * 0.48, s * 0.8);
        cone.lineTo(s * 0.28, s * 0.62);
        cone.lineTo(s * 0.12, s * 0.62);
        cone.closeSubpath();
        p.setBrush(color);
        p.drawPath(cone);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(s * 0.44, s * 0.32, s * 0.24, s * 0.36), -50 * 16, 100 * 16);
        p.drawArc(QRectF(s * 0.44, s * 0.18, s * 0.44, s * 0.64), -50 * 16, 100 * 16);
    });
}

QIcon gear(const QColor &color)
{
    return draw(color, [](QPainter &p, qreal s) {
        const QPointF c(s / 2, s / 2);
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            const qreal a = qDegreesToRadians(i * 360.0 / teeth);
            p.drawLine(QPointF(c.x() + std::cos(a) * s * 0.28, c.y() + std::sin(a) * s * 0.28),
                       QPointF(c.x() + std::cos(a) * s * 0.42, c.y() + std::sin(a) * s * 0.42));
        }
        p.drawEllipse(c, s * 0.26, s * 0.26);
        p.drawEllipse(c, s * 0.1, s * 0.1);
    });
}

} // namespace Icons
