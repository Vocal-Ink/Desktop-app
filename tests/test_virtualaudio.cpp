#include "platform/VirtualAudio.h"

#include <QProcess>
#include <QTest>

namespace {
QString pactl(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("pactl"), args);
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}
} // namespace

class TestVirtualAudio : public QObject
{
    Q_OBJECT
private slots:
    void instructionsMentionACable()
    {
        const QString html = VirtualAudio::setupInstructions();
        QVERIFY(html.contains(QLatin1String("Vocal Ink")));
    }

    void recognisesCommonCables()
    {
        QVERIFY(VirtualAudio::looksLikeVirtualCable(QStringLiteral("CABLE Input (VB-Audio Virtual Cable)")));
        QVERIFY(VirtualAudio::looksLikeVirtualCable(QStringLiteral("BlackHole 2ch")));
        QVERIFY(VirtualAudio::looksLikeVirtualCable(QStringLiteral("Vocal Ink Voice")));
        QVERIFY(!VirtualAudio::looksLikeVirtualCable(QStringLiteral("Speakers (Realtek High Definition Audio)")));
    }

    // Needs a running PulseAudio/PipeWire server; skipped elsewhere (e.g. CI).
    void createsAndRemovesLinuxVirtualMic()
    {
        if (!VirtualAudio::canCreateVirtualMic() || pactl({QStringLiteral("info")}).isEmpty())
            QSKIP("no PulseAudio/PipeWire server");
        QString error;
        VirtualAudio::removeVirtualMic(&error);
        QVERIFY(!VirtualAudio::virtualMicExists());

        QVERIFY2(VirtualAudio::createVirtualMic(&error), qPrintable(error));
        QVERIFY(VirtualAudio::virtualMicExists());
        // Descriptions with spaces must survive PulseAudio's argument parsing.
        QVERIFY(pactl({QStringLiteral("list"), QStringLiteral("sinks")}).contains(QLatin1String("Description: Vocal Ink Voice")));
        QVERIFY(pactl({QStringLiteral("list"), QStringLiteral("sources")}).contains(QLatin1String("Description: Vocal Ink Microphone")));
        QVERIFY2(VirtualAudio::createVirtualMic(&error), "creating twice is harmless");

        QVERIFY2(VirtualAudio::removeVirtualMic(&error), qPrintable(error));
        QVERIFY(!VirtualAudio::virtualMicExists());
    }
};

QTEST_GUILESS_MAIN(TestVirtualAudio)
#include "test_virtualaudio.moc"
