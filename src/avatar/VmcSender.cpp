#include "avatar/VmcSender.h"

#include <QHostInfo>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

const QByteArray kValAddress = QByteArrayLiteral("/VMC/Ext/Blend/Val");
const QByteArray kApplyAddress = QByteArrayLiteral("/VMC/Ext/Blend/Apply");
constexpr int kResendMs = 500;       // identical frames are repeated this often at most
constexpr int kSendingWindowMs = 1000;

// OSC-string: the bytes, a NUL, then NULs up to a multiple of four.
void appendOscString(QByteArray &out, const QByteArray &s)
{
    const qsizetype padded = (s.size() / 4 + 1) * 4;
    out += s;
    out.append(padded - s.size(), '\0');
}

void appendInt32(QByteArray &out, qint32 value)
{
    char bytes[4];
    qToBigEndian<qint32>(value, bytes);
    out.append(bytes, 4);
}

quint32 floatBits(float value)
{
    quint32 bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void appendFloat32(QByteArray &out, float value)
{
    char bytes[4];
    qToBigEndian<quint32>(floatBits(value), bytes);
    out.append(bytes, 4);
}

float clamp01(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

} // namespace

QList<VmcSender::Preset> VmcSender::presets()
{
    return {{QStringLiteral("vseeface"), QStringLiteral("VSeeFace"), 39539},
            {QStringLiteral("warudo"), QStringLiteral("Warudo"), 39539},
            {QStringLiteral("vnyan"), QStringLiteral("VNyan"), 39539},
            {QStringLiteral("vmc"), QStringLiteral("VirtualMotionCapture"), 39540},
            {QStringLiteral("custom"), QString(), 39539}}; // "custom" is named in QML
}

VmcSender::VmcSender(QObject *parent)
    : QObject(parent)
    , m_sendingTimer(new QTimer(this))
{
    m_sendingTimer->setSingleShot(true);
    m_sendingTimer->setInterval(kSendingWindowMs);
    connect(m_sendingTimer, &QTimer::timeout, this, [this] {
        if (!m_sending)
            return;
        m_sending = false;
        emit statusChanged();
    });
    rebuildFrame();
}

VmcSender::~VmcSender()
{
    if (m_lookupId >= 0)
        QHostInfo::abortHostLookup(m_lookupId);
}

void VmcSender::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    if (!enabled) {
        release(); // don't leave the avatar with its mouth open
        m_enabled = false;
        delete m_socket;
        m_socket = nullptr;
        m_haveLast = false;
        m_sendingTimer->stop();
        if (m_sending) {
            m_sending = false;
            emit statusChanged();
        }
    } else {
        m_enabled = true;
        resolveHost();
    }
    emit configChanged();
}

void VmcSender::setTarget(const QString &host, quint16 port)
{
    const QString h = host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : host.trimmed();
    const int p = port ? int(port) : 39539;
    if (h == m_host && p == m_port)
        return;
    release(); // the old receiver gets a closed mouth
    m_host = h;
    m_port = p;
    m_haveLast = false;
    if (m_enabled)
        resolveHost();
    emit configChanged();
}

void VmcSender::setBlendset(const QString &blendset)
{
    const QString b = blendset == QLatin1String("vrm1") ? QStringLiteral("vrm1") : QStringLiteral("vrm0");
    if (b == m_blendset)
        return;
    release();
    m_blendset = b;
    rebuildFrame();
}

void VmcSender::setGain(float gain)
{
    m_gain = std::isfinite(gain) ? std::clamp(gain, 0.2f, 2.0f) : 1.0f;
}

void VmcSender::setExpression(const QString &name)
{
    const QString n = name.trimmed();
    if (n == m_expression)
        return;
    release();
    m_expression = n;
    rebuildFrame();
}

QVariantList VmcSender::presetList() const
{
    QVariantList out;
    for (const Preset &p : presets())
        out.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("port"), p.port}});
    return out;
}

QStringList VmcSender::mouthShapeNames(const QString &blendset)
{
    if (blendset == QLatin1String("vrm1"))
        return {QStringLiteral("aa"), QStringLiteral("ih"), QStringLiteral("ou"), QStringLiteral("ee"), QStringLiteral("oh")};
    return {QStringLiteral("A"), QStringLiteral("I"), QStringLiteral("U"), QStringLiteral("E"), QStringLiteral("O")};
}

std::array<float, 5> VmcSender::mouthWeights(float open, AvatarController::Viseme viseme)
{
    using V = AvatarController::Viseme;
    enum { A, I, U, E, O };
    // Each shape borrows a little from its neighbours so changes look softer.
    std::array<float, 5> w{};
    switch (viseme) {
    case V::Rest:
    case V::A: w[A] = 1.0f; w[E] = 0.1f; w[O] = 0.1f; break;
    case V::I: w[I] = 1.0f; w[E] = 0.25f; break;
    case V::U: w[U] = 1.0f; w[O] = 0.25f; break;
    case V::E: w[E] = 1.0f; w[I] = 0.2f; w[A] = 0.1f; break;
    case V::O: w[O] = 1.0f; w[U] = 0.25f; w[A] = 0.1f; break;
    }
    const float o = clamp01(open);
    for (float &x : w)
        x *= o;
    return w;
}

void VmcSender::rebuildFrame()
{
    const QStringList names = mouthShapeNames(m_blendset);
    QList<QByteArray> messages;
    for (const QString &name : names)
        messages << oscMessage(kValAddress, {name, 0.0f});
    if (!m_expression.isEmpty())
        messages << oscMessage(kValAddress, {m_expression, 0.0f});
    messages << oscMessage(kApplyAddress, {});
    m_frame = oscBundle(messages);

    // Every Val message ends with its float: remember where they are.
    int pos = 16; // "#bundle\0" + time tag
    for (int i = 0; i < messages.size(); ++i) {
        const int end = pos + 4 + int(messages.at(i).size());
        if (i < 5)
            m_mouthOffsets[size_t(i)] = end - 4;
        else if (i == 5 && !m_expression.isEmpty())
            m_expressionOffset = end - 4;
        pos = end;
    }
    if (m_expression.isEmpty())
        m_expressionOffset = -1;
    m_haveLast = false;
}

void VmcSender::resolveHost()
{
    if (m_lookupId >= 0) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    m_address = QHostAddress(m_host);
    if (!m_address.isNull())
        return;
    if (m_host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
        m_address = QHostAddress(QHostAddress::LocalHost);
        return;
    }
    const QString host = m_host;
    m_lookupId = QHostInfo::lookupHost(host, this, [this, host](const QHostInfo &info) {
        m_lookupId = -1;
        if (host != m_host)
            return;
        const QList<QHostAddress> addresses = info.addresses();
        for (const QHostAddress &a : addresses) {
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                m_address = a;
                return;
            }
        }
        if (!addresses.isEmpty())
            m_address = addresses.first();
    });
}

void VmcSender::sendMouth(float open, AvatarController::Viseme viseme, bool talking)
{
    if (!m_enabled)
        return;
    std::array<float, 5> w = mouthWeights(open, viseme);
    for (float &x : w)
        x = clamp01(x * m_gain);
    writeFrame(w, talking && m_expressionOffset >= 0 ? 1.0f : 0.0f, false);
}

void VmcSender::release()
{
    if (!m_enabled || !m_haveLast)
        return;
    const bool allZero = std::all_of(m_last.cbegin(), m_last.cend(), [](float v) { return v == 0.0f; });
    if (allZero)
        return;
    writeFrame({}, 0.0f, true);
}

void VmcSender::writeFrame(const std::array<float, 5> &mouth, float expression, bool force)
{
    if (!m_enabled || m_address.isNull())
        return;
    const std::array<float, 6> values{mouth[0], mouth[1], mouth[2], mouth[3], mouth[4], expression};
    if (!force && m_haveLast && values == m_last && m_lastSend.isValid() && m_lastSend.elapsed() < kResendMs)
        return;

    char *data = m_frame.data();
    for (size_t i = 0; i < 5; ++i)
        qToBigEndian<quint32>(floatBits(values[i]), data + m_mouthOffsets[i]);
    if (m_expressionOffset >= 0)
        qToBigEndian<quint32>(floatBits(expression), data + m_expressionOffset);

    if (!m_socket)
        m_socket = new QUdpSocket(this);
    if (m_socket->writeDatagram(m_frame, m_address, quint16(m_port)) < 0)
        return; // nothing listening is fine for UDP; real errors just skip the frame
    m_last = values;
    m_haveLast = true;
    m_lastSend.start();
    markSent();
}

void VmcSender::markSent()
{
    m_sendingTimer->start();
    if (!m_sending) {
        m_sending = true;
        emit statusChanged();
    }
}

QByteArray VmcSender::oscMessage(const QByteArray &address, const QVariantList &args)
{
    QByteArray tags(1, ',');
    QByteArray data;
    for (const QVariant &arg : args) {
        switch (arg.typeId()) {
        case QMetaType::Bool:
            tags += arg.toBool() ? 'T' : 'F';
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Long:
        case QMetaType::ULong:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            tags += 'i';
            appendInt32(data, qint32(arg.toLongLong()));
            break;
        case QMetaType::Float:
        case QMetaType::Double:
            tags += 'f';
            appendFloat32(data, arg.toFloat());
            break;
        case QMetaType::QByteArray:
            tags += 's';
            appendOscString(data, arg.toByteArray());
            break;
        default:
            tags += 's';
            appendOscString(data, arg.toString().toUtf8());
            break;
        }
    }
    QByteArray out;
    out.reserve(address.size() + tags.size() + data.size() + 8);
    appendOscString(out, address);
    appendOscString(out, tags);
    out += data;
    return out;
}

QByteArray VmcSender::oscBundle(const QList<QByteArray> &messages)
{
    QByteArray out("#bundle", 8); // with its NUL
    out.append(7, '\0');
    out.append('\1'); // time tag 1 = "immediately"
    for (const QByteArray &m : messages) {
        appendInt32(out, qint32(m.size()));
        out += m;
    }
    return out;
}
