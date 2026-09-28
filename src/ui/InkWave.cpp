#include "ui/InkWave.h"

#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QTimer>
#include <QtMath>

namespace {
constexpr int kSamples = 120;   // ~2 s of history at 60 Hz
constexpr int kBarEvery = 4;    // one bar per this many samples in "bars" style

float smoothstep(float edge0, float edge1, float x)
{
    const float t = qBound(0.0f, (x - edge0) / (edge1 - edge0), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Scene-graph vertex colours are premultiplied.
void paintVertex(QSGGeometry::ColoredPoint2D &v, float x, float y, const QColor &c, float alpha)
{
    const float a = float(c.alphaF()) * qBound(0.0f, alpha, 1.0f);
    v.set(x, y, uchar(c.red() * a), uchar(c.green() * a), uchar(c.blue() * a), uchar(255 * a));
}

QColor mix(const QColor &a, const QColor &b, float t)
{
    t = qBound(0.0f, t, 1.0f);
    return QColor::fromRgbF(float(a.redF()) + (float(b.redF()) - float(a.redF())) * t,
                            float(a.greenF()) + (float(b.greenF()) - float(a.greenF())) * t,
                            float(a.blueF()) + (float(b.blueF()) - float(a.blueF())) * t,
                            float(a.alphaF()) + (float(b.alphaF()) - float(a.alphaF())) * t);
}
} // namespace

InkWave::InkWave(QQuickItem *parent)
    : QQuickItem(parent)
    , m_timer(new QTimer(this))
    , m_history(kSamples, 0.0f)
{
    setFlag(ItemHasContents, true);
    m_timer->setInterval(16);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &InkWave::tick);
}

void InkWave::setLevel(qreal level)
{
    level = qBound<qreal>(0.0, level, 1.0);
    if (qFuzzyCompare(level + 1.0, m_level + 1.0))
        return;
    m_level = level;
    emit levelChanged();
    if (m_running && !m_timer->isActive())
        m_timer->start();
}

void InkWave::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
    if (running)
        m_timer->start();
}

void InkWave::setColor(const QColor &c)
{
    if (m_color == c)
        return;
    m_color = c;
    emit colorChanged();
    update();
}

void InkWave::setSheen(const QColor &c)
{
    if (m_sheen == c)
        return;
    m_sheen = c;
    emit colorChanged();
    update();
}

void InkWave::setStyle(const QString &s)
{
    if (m_style == s)
        return;
    m_style = s;
    emit styleChanged();
    update();
}

void InkWave::setThickness(qreal t)
{
    if (qFuzzyCompare(m_thickness, t))
        return;
    m_thickness = t;
    emit thicknessChanged();
    update();
}

void InkWave::setAnimated(bool a)
{
    if (m_animated == a)
        return;
    m_animated = a;
    emit animatedChanged();
    update();
}

bool InkWave::idle() const
{
    for (float v : m_history) {
        if (v > 0.002f)
            return false;
    }
    return true;
}

void InkWave::tick()
{
    // Fast attack, slow release: speech reads as strokes, not flicker.
    const float target = m_running ? float(m_level) : 0.0f;
    const float k = target > m_smoothed ? 0.55f : 0.12f;
    m_smoothed += (target - m_smoothed) * k;
    // Perceptual curve so quiet voices still leave visible ink.
    const float shaped = std::pow(qBound(0.0f, m_smoothed, 1.0f), 0.6f);
    if (m_animated) {
        m_history.removeFirst();
        m_history.append(shaped);
        m_phase += 0.09f;
    } else {
        m_history.fill(shaped); // reduced motion: the stroke breathes in place
    }
    update();
    if (!m_running && idle())
        m_timer->stop();
}

QSGNode *InkWave::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGGeometryNode *>(oldNode);
    if (!node) {
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    QSGGeometry *g = node->geometry();
    const float w = float(width());
    const float h = float(height());
    const float mid = h / 2.0f;
    const int n = int(m_history.size());
    if (w <= 1.0f || h <= 1.0f || n < 2) {
        g->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry);
        return node;
    }
    const float maxHalf = qMin(float(m_thickness), mid - 1.0f);

    if (m_style == QLatin1String("bars")) {
        const int bars = n / kBarEvery;
        const float slot = w / float(bars);
        const float barW = qMax(2.0f, slot * 0.55f);
        g->allocate(bars * 6);
        auto *v = g->vertexDataAsColoredPoint2D();
        for (int b = 0; b < bars; ++b) {
            float peak = 0.0f;
            for (int i = 0; i < kBarEvery; ++i)
                peak = qMax(peak, m_history.at(b * kBarEvery + i));
            const float half = qMax(1.0f, peak * maxHalf);
            const float x0 = b * slot + (slot - barW) / 2.0f;
            const float x1 = x0 + barW;
            const float age = float(b + 1) / float(bars);
            const QColor c = mix(m_color, m_sheen, peak * 0.8f);
            const float alpha = 0.25f + 0.75f * age;
            paintVertex(v[0], x0, mid - half, c, alpha);
            paintVertex(v[1], x1, mid - half, c, alpha);
            paintVertex(v[2], x0, mid + half, c, alpha);
            paintVertex(v[3], x1, mid - half, c, alpha);
            paintVertex(v[4], x1, mid + half, c, alpha);
            paintVertex(v[5], x0, mid + half, c, alpha);
            v += 6;
        }
        node->markDirty(QSGNode::DirtyGeometry);
        return node;
    }

    // Ink ribbon: two triangles per segment.
    g->allocate((n - 1) * 6);
    auto *v = g->vertexDataAsColoredPoint2D();
    struct P { float x, top, bottom; QColor c; float a; };
    auto point = [&](int i) {
        const float t = float(i) / float(n - 1);
        const float amp = m_history.at(i);
        // Brush pressure: taper both ends, swell with the voice.
        const float taper = smoothstep(0.0f, 0.10f, t) * (1.0f - 0.55f * smoothstep(0.92f, 1.0f, t));
        const float half = (0.9f + amp * maxHalf) * taper;
        const float wobble = m_animated ? std::sin(m_phase + float(i) * 0.21f) * amp * maxHalf * 0.28f : 0.0f;
        const float y = mid + wobble;
        const QColor c = mix(m_color, m_sheen, qMax(0.0f, amp - 0.35f) * 1.4f);
        const float alpha = 0.18f + 0.82f * smoothstep(0.0f, 0.75f, t);
        return P{t * w, y - half, y + half, c, alpha};
    };
    P prev = point(0);
    for (int i = 1; i < n; ++i) {
        const P cur = point(i);
        paintVertex(v[0], prev.x, prev.top, prev.c, prev.a);
        paintVertex(v[1], cur.x, cur.top, cur.c, cur.a);
        paintVertex(v[2], prev.x, prev.bottom, prev.c, prev.a);
        paintVertex(v[3], cur.x, cur.top, cur.c, cur.a);
        paintVertex(v[4], cur.x, cur.bottom, cur.c, cur.a);
        paintVertex(v[5], prev.x, prev.bottom, prev.c, prev.a);
        v += 6;
        prev = cur;
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
