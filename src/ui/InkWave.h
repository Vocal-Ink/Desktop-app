#pragma once

#include <QColor>
#include <QQuickItem>
#include <QVector>

class QTimer;

// The live voice drawn as a single calligraphic brush stroke: its thickness
// follows the loudness of the last couple of seconds, newest on the right,
// older ink fading out to the left. "bars" draws a classic level meter.
// Rendered with scene-graph geometry so it stays cheap at 60 fps.
class InkWave : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QColor sheen READ sheen WRITE setSheen NOTIFY colorChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(qreal thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY animatedChanged)
public:
    explicit InkWave(QQuickItem *parent = nullptr);

    qreal level() const { return m_level; }
    void setLevel(qreal level);
    bool running() const { return m_running; }
    void setRunning(bool running);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
    QColor sheen() const { return m_sheen; }
    void setSheen(const QColor &c);
    QString style() const { return m_style; }
    void setStyle(const QString &s);
    qreal thickness() const { return m_thickness; }
    void setThickness(qreal t);
    bool animated() const { return m_animated; }
    void setAnimated(bool a);

signals:
    void levelChanged();
    void runningChanged();
    void colorChanged();
    void styleChanged();
    void thicknessChanged();
    void animatedChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void tick();
    bool idle() const;

    QTimer *m_timer;
    QVector<float> m_history; // oldest first
    qreal m_level = 0.0;
    float m_smoothed = 0.0f;
    float m_phase = 0.0f;
    bool m_running = false;
    bool m_animated = true;
    QColor m_color = QColor(0x8c, 0x52, 0xff);
    QColor m_sheen = QColor(0xff, 0x7a, 0xd9);
    QString m_style = QStringLiteral("ink");
    qreal m_thickness = 18.0;
};
