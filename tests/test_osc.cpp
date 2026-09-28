#include "avatar/VmcSender.h"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>
#include <QtEndian>
#include <cstring>

namespace {

using Viseme = AvatarController::Viseme;

QByteArray hex(const char *h)
{
    return QByteArray::fromHex(h);
}

// Minimal OSC decoder, written independently of the encoder.
struct Osc
{
    QByteArray address;
    QByteArray tags;
    QVariantList args;
};

QByteArray readString(const QByteArray &b, int &pos)
{
    const int end = int(b.indexOf('\0', pos));
    const QByteArray s = b.mid(pos, end - pos);
    pos = (end / 4 + 1) * 4;
    return s;
}

Osc decodeMessage(const QByteArray &b)
{
    Osc m;
    int pos = 0;
    m.address = readString(b, pos);
    m.tags = readString(b, pos);
    for (int i = 1; i < m.tags.size(); ++i) {
        switch (m.tags.at(i)) {
        case 's': m.args << QString::fromUtf8(readString(b, pos)); break;
        case 'f': {
            const quint32 bits = qFromBigEndian<quint32>(b.constData() + pos);
            float f;
            std::memcpy(&f, &bits, 4);
            m.args << f;
            pos += 4;
            break;
        }
        case 'i': m.args << qFromBigEndian<qint32>(b.constData() + pos); pos += 4; break;
        case 'T': m.args << true; break;
        case 'F': m.args << false; break;
        default: break;
        }
    }
    return m;
}

QList<Osc> decodeBundle(const QByteArray &b)
{
    QList<Osc> out;
    if (!b.startsWith(QByteArray("#bundle\0", 8)) || b.size() < 16)
        return out;
    int pos = 16;
    while (pos + 4 <= b.size()) {
        const int size = qFromBigEndian<qint32>(b.constData() + pos);
        pos += 4;
        out << decodeMessage(b.mid(pos, size));
        pos += size;
    }
    return out;
}

// Blend values of one frame, in order, plus whether it ended with Apply.
struct Frame
{
    QList<QPair<QString, float>> values;
    bool applied = false;
    float value(const QString &name) const
    {
        for (const auto &v : values) {
            if (v.first == name)
                return v.second;
        }
        return -1.0f;
    }
};

Frame toFrame(const QByteArray &datagram)
{
    Frame f;
    const QList<Osc> messages = decodeBundle(datagram);
    for (const Osc &m : messages) {
        if (m.address == "/VMC/Ext/Blend/Val" && m.tags == ",sf")
            f.values.append({m.args.at(0).toString(), m.args.at(1).toFloat()});
        else if (m.address == "/VMC/Ext/Blend/Apply")
            f.applied = true;
    }
    return f;
}

struct Receiver
{
    QUdpSocket socket;
    Receiver() { socket.bind(QHostAddress::LocalHost, 0); }
    quint16 port() const { return socket.localPort(); }
    QList<QByteArray> take(int waitMs = 300)
    {
        QList<QByteArray> out;
        (void)QTest::qWaitFor([this] { return socket.hasPendingDatagrams(); }, waitMs);
        QTest::qWait(20);
        while (socket.hasPendingDatagrams())
            out << socket.receiveDatagram().data();
        return out;
    }
};

} // namespace

class TestOsc : public QObject
{
    Q_OBJECT
private slots:
    void blendValMessageIsByteExact()
    {
        const QByteArray m = VmcSender::oscMessage("/VMC/Ext/Blend/Val", {QStringLiteral("A"), 0.5f});
        QCOMPARE(m.size(), 32);
        QCOMPARE(m, hex("2F564D432F4578742F426C656E642F56616C0000" "2C736600" "41000000" "3F000000"));
    }

    void applyMessageIsByteExact()
    {
        const QByteArray m = VmcSender::oscMessage("/VMC/Ext/Blend/Apply", {});
        QCOMPARE(m.size(), 28);
        QCOMPARE(m.left(20), QByteArray("/VMC/Ext/Blend/Apply"));
        QCOMPARE(m.mid(20, 4), QByteArray(4, '\0'));
        QCOMPARE(m.mid(24), hex("2C000000"));
    }

    void stringsArePaddedToFourBytes_data()
    {
        QTest::addColumn<QString>("s");
        QTest::addColumn<QByteArray>("encoded");
        QTest::newRow("empty") << QString() << hex("00000000");
        QTest::newRow("1") << QStringLiteral("a") << hex("61000000");
        QTest::newRow("2") << QStringLiteral("ab") << hex("61620000");
        QTest::newRow("3") << QStringLiteral("abc") << hex("61626300");
        QTest::newRow("4") << QStringLiteral("abcd") << hex("6162636400000000");
    }

    void stringsArePaddedToFourBytes()
    {
        QFETCH(QString, s);
        QFETCH(QByteArray, encoded);
        const QByteArray m = VmcSender::oscMessage("/x", {s});
        // "/x" + 2 NULs, ",s" + 2 NULs, then the string.
        QCOMPARE(m.left(8), hex("2F7800002C730000"));
        QCOMPARE(m.mid(8), encoded);
        QCOMPARE(m.size() % 4, 0);
        // Addresses follow the same rule.
        QCOMPARE(VmcSender::oscMessage(QByteArray("/abc"), {}).left(8), hex("2F61626300000000"));
    }

    void numbersAndBooleans()
    {
        const QByteArray m = VmcSender::oscMessage("/n", {1, -2, 1.5f, 0.25, true, false});
        // ",iiffTF" is 7 characters: padded to 8.
        const QByteArray expected = hex("2F6E0000" "2C696966665446" "00" "00000001" "FFFFFFFE" "3FC00000" "3E800000");
        QCOMPARE(m, expected);
        const Osc d = decodeMessage(m);
        QCOMPARE(d.tags, QByteArray(",iiffTF"));
        QCOMPARE(d.args.at(4).toBool(), true);
        QCOMPARE(d.args.at(5).toBool(), false);
    }

    void bundleLayout()
    {
        const QByteArray a = VmcSender::oscMessage("/VMC/Ext/Blend/Val", {QStringLiteral("A"), 0.5f});
        const QByteArray b = VmcSender::oscMessage("/VMC/Ext/Blend/Apply", {});
        const QByteArray bundle = VmcSender::oscBundle({a, b});
        QCOMPARE(bundle.left(8), QByteArray("#bundle\0", 8));
        QCOMPARE(bundle.mid(8, 8), hex("0000000000000001"));
        QCOMPARE(bundle.mid(16, 4), hex("00000020"));
        QCOMPARE(bundle.mid(20, 32), a);
        QCOMPARE(bundle.mid(52, 4), hex("0000001C"));
        QCOMPARE(bundle.mid(56), b);
        QCOMPARE(bundle.size(), 16 + 4 + 32 + 4 + 28);
    }

    void sendsOneBundlePerFrame()
    {
        Receiver rx;
        VmcSender vmc;
        QSignalSpy sending(&vmc, &VmcSender::statusChanged);
        vmc.setTarget(QStringLiteral("127.0.0.1"), rx.port());
        vmc.sendMouth(0.5f, Viseme::A, true); // disabled: nothing
        QVERIFY(rx.take(100).isEmpty());

        vmc.setEnabled(true);
        vmc.sendMouth(0.8f, Viseme::O, true);
        const QList<QByteArray> got = rx.take();
        QCOMPARE(got.size(), 1);
        const Frame f = toFrame(got.first());
        QVERIFY(f.applied);
        QCOMPARE(f.values.size(), 5); // explicit zeros for the unused shapes
        QCOMPARE(f.values.at(0).first, QStringLiteral("A"));
        QCOMPARE(f.values.at(4).first, QStringLiteral("O"));
        QCOMPARE(f.value(QStringLiteral("O")), 0.8f);
        QVERIFY(f.value(QStringLiteral("U")) > 0.0f && f.value(QStringLiteral("U")) < 0.8f); // neighbour
        QCOMPARE(f.value(QStringLiteral("I")), 0.0f);
        QCOMPARE(f.value(QStringLiteral("E")), 0.0f);
        QVERIFY(vmc.isSending());
        QVERIFY(!sending.isEmpty());
    }

    void identicalFramesAreThrottled()
    {
        Receiver rx;
        VmcSender vmc;
        vmc.setTarget(QStringLiteral("127.0.0.1"), rx.port());
        vmc.setEnabled(true);
        for (int i = 0; i < 5; ++i)
            vmc.sendMouth(0.4f, Viseme::A, true);
        QCOMPARE(rx.take().size(), 1);
        vmc.sendMouth(0.5f, Viseme::A, true); // changed
        QCOMPARE(rx.take().size(), 1);
        QTest::qWait(550);
        vmc.sendMouth(0.5f, Viseme::A, true); // same, but half a second later
        QCOMPARE(rx.take().size(), 1);
    }

    void vrm1NamesGainAndExpression()
    {
        Receiver rx;
        VmcSender vmc;
        vmc.setTarget(QStringLiteral("127.0.0.1"), rx.port());
        vmc.setBlendset(QStringLiteral("vrm1"));
        vmc.setExpression(QStringLiteral("happy"));
        vmc.setGain(2.0f);
        vmc.setEnabled(true);

        vmc.sendMouth(0.3f, Viseme::I, true);
        Frame f = toFrame(rx.take().value(0));
        QStringList names;
        for (const auto &v : f.values)
            names << v.first;
        QCOMPARE(names, QStringList({"aa", "ih", "ou", "ee", "oh", "happy"}));
        QVERIFY(std::fabs(f.value(QStringLiteral("ih")) - 0.6f) < 1e-6f); // gain 2
        QCOMPARE(f.value(QStringLiteral("happy")), 1.0f);

        vmc.sendMouth(0.9f, Viseme::A, false); // clamped, expression released
        f = toFrame(rx.take().value(0));
        QCOMPARE(f.value(QStringLiteral("aa")), 1.0f);
        QCOMPARE(f.value(QStringLiteral("happy")), 0.0f);
    }

    void releaseSendsZerosOnce()
    {
        Receiver rx;
        VmcSender vmc;
        vmc.setTarget(QStringLiteral("127.0.0.1"), rx.port());
        vmc.setExpression(QStringLiteral("Joy"));
        vmc.setEnabled(true);
        vmc.release(); // nothing sent yet: nothing to release
        QVERIFY(rx.take(100).isEmpty());

        vmc.sendMouth(0.7f, Viseme::E, true);
        rx.take();
        vmc.release();
        const QList<QByteArray> got = rx.take();
        QCOMPARE(got.size(), 1);
        const Frame f = toFrame(got.first());
        QCOMPARE(f.values.size(), 6);
        for (const auto &v : f.values)
            QCOMPARE(v.second, 0.0f);
        vmc.release();
        QVERIFY(rx.take(100).isEmpty());

        // Switching off also closes the mouth.
        vmc.sendMouth(0.7f, Viseme::E, true);
        rx.take();
        vmc.setEnabled(false);
        QCOMPARE(toFrame(rx.take().value(0)).value(QStringLiteral("E")), 0.0f);
        QTRY_VERIFY_WITH_TIMEOUT(!vmc.isSending(), 1500);
    }

    void mouthWeightsFollowTheShape()
    {
        const auto w = VmcSender::mouthWeights(1.0f, Viseme::U);
        QCOMPARE(w[2], 1.0f);
        QVERIFY(w[4] > 0.0f); // O is U's neighbour
        QCOMPARE(w[0], 0.0f);
        const auto rest = VmcSender::mouthWeights(0.5f, Viseme::Rest);
        QCOMPARE(rest[0], 0.5f); // open without a shape counts as A
        const auto closed = VmcSender::mouthWeights(0.0f, Viseme::I);
        for (float x : closed)
            QCOMPARE(x, 0.0f);
    }

    void presets()
    {
        const QVariantList list = VmcSender().presetList();
        QCOMPARE(list.size(), 5);
        QCOMPARE(list.at(3).toMap().value(QStringLiteral("port")).toInt(), 39540);
    }
};

QTEST_GUILESS_MAIN(TestOsc)
#include "test_osc.moc"
