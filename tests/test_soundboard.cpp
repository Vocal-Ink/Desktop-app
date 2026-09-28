#include "audio/AudioConvert.h"
#include "audio/AudioPlayer.h"
#include "audio/Soundboard.h"
#include "support/TestUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kRate = 22050;

struct Rig
{
    QTemporaryDir tmp;
    AudioPlayer player;
    TestUtil::FakeLane *lane = nullptr;
    QString sourceDir;
    QString storeDir;

    Rig()
    {
        player.setLaneFactory([this](const QByteArray &, QObject *parent) {
            lane = new TestUtil::FakeLane(parent, kRate);
            return lane;
        });
        sourceDir = tmp.filePath(QStringLiteral("incoming"));
        storeDir = tmp.filePath(QStringLiteral("store/sounds"));
        QDir().mkpath(sourceDir);
    }

    QString writeFile(const QString &name, const QByteArray &bytes) const
    {
        const QString path = QDir(sourceDir).filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(bytes);
        return path;
    }

    QString writeWav(const QString &name, double seconds = 0.2) const
    {
        return writeFile(name, AudioConvert::makeWav16(TestUtil::sine(440, kRate, seconds, 0.5f), kRate));
    }
};

} // namespace

class TestSoundboard : public QObject
{
    Q_OBJECT
private slots:
    void addCopiesIntoTheStore()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        QVERIFY(board.load()); // no store yet: empty, fine
        QVERIFY(board.sounds().isEmpty());
        QSignalSpy changed(&board, &Soundboard::changed);

        const QString source = rig.writeWav(QStringLiteral("Air Horn!.wav"));
        QString error;
        const QString id = board.addFile(source, &error);
        QCOMPARE(id, QStringLiteral("air-horn"));
        QVERIFY(error.isEmpty());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(board.sounds().size(), 1);
        const Sound &s = board.sounds().first();
        QCOMPARE(s.name, QStringLiteral("Air Horn!"));
        QCOMPARE(s.gain, 1.0f);
        QVERIFY(QFileInfo::exists(s.file));
        QCOMPARE(QFileInfo(s.file).absolutePath(), QDir(rig.storeDir).absolutePath());
        QVERIFY(QFileInfo(s.file).absoluteFilePath() != QFileInfo(source).absoluteFilePath());
        QVERIFY(QFileInfo::exists(QDir(rig.storeDir).filePath(QStringLiteral("sounds.json"))));

        // Same name again: a unique id and file.
        const QString second = board.addFile(source);
        QCOMPARE(second, QStringLiteral("air-horn-2"));
        QVERIFY(board.sounds().at(1).file != s.file);
        QCOMPARE(board.indexOf(second), 1);

        // Names without usable characters still get an id.
        QCOMPARE(board.addFile(rig.writeWav(QStringLiteral("日本.wav"))), QStringLiteral("sound"));
    }

    void rejectsBadFiles()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        QString error;
        QVERIFY(board.addFile(rig.writeFile(QStringLiteral("notes.txt"), "hello"), &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("notes.txt")));
        QVERIFY(board.addFile(QDir(rig.sourceDir).filePath(QStringLiteral("missing.wav")), &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("missing.wav")));
        QVERIFY(board.sounds().isEmpty());
    }

    void jsonRoundTrip()
    {
        Rig rig;
        QString id;
        {
            Soundboard board(&rig.player, rig.storeDir);
            id = board.addFile(rig.writeWav(QStringLiteral("laugh.wav")));
            board.addFile(rig.writeWav(QStringLiteral("drum.wav")));
            Sound s = board.sounds().first();
            s.name = QStringLiteral("Big laugh");
            s.hotkey = QStringLiteral("Ctrl+Alt+L");
            s.gain = 0.5f;
            s.color = QStringLiteral("#ff8800");
            s.file.clear(); // keeps the existing file
            board.update(s);
            board.move(1, 0);
        }

        QFile f(QDir(rig.storeDir).filePath(QStringLiteral("sounds.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QLatin1String("version")).toInt(), 1);
        const QJsonArray list = root.value(QLatin1String("sounds")).toArray();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(1).toObject().value(QLatin1String("file")).toString(), QStringLiteral("laugh.wav")); // relative

        Soundboard again(&rig.player, rig.storeDir);
        QVERIFY(again.load());
        QCOMPARE(again.sounds().size(), 2);
        QCOMPARE(again.sounds().at(0).id, QStringLiteral("drum"));
        const Sound &s = again.sounds().at(1);
        QCOMPARE(s.id, id);
        QCOMPARE(s.name, QStringLiteral("Big laugh"));
        QCOMPARE(s.hotkey, QStringLiteral("Ctrl+Alt+L"));
        QCOMPARE(s.gain, 0.5f);
        QCOMPARE(s.color, QStringLiteral("#ff8800"));
        QCOMPARE(QFileInfo(s.file).absoluteFilePath(), QFileInfo(QDir(rig.storeDir).filePath(QStringLiteral("laugh.wav"))).absoluteFilePath());
        QVERIFY(QFileInfo::exists(s.file));
    }

    void corruptStoreFailsToLoad()
    {
        Rig rig;
        QDir().mkpath(rig.storeDir);
        QFile f(QDir(rig.storeDir).filePath(QStringLiteral("sounds.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();
        Soundboard board(&rig.player, rig.storeDir);
        QVERIFY(!board.load());
        QVERIFY(board.sounds().isEmpty());
    }

    void playMixesIntoThePlayer()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        const QString id = board.addFile(rig.writeWav(QStringLiteral("beep.wav"), 0.2));
        QSignalSpy playing(&board, &Soundboard::playingChanged);

        board.play(id);
        QVERIFY(board.isPlaying(id));
        QCOMPARE(playing.size(), 1);
        QCOMPARE(playing.first().at(0).toString(), id);
        QVERIFY(playing.first().at(1).toBool());
        QCOMPARE(rig.lane->soundsPlayed.size(), 1);

        const QVector<float> out = rig.lane->pull(kRate / 10);
        const float peak = AudioConvert::peak(out.constData(), out.size());
        QVERIFY2(std::fabs(peak - 0.5f) < 0.01f, qPrintable(QString::number(peak)));
        QVERIFY(std::fabs(TestUtil::zeroCrossingFrequency(out, kRate) - 440.0) < 15.0);

        // Playing again while it plays restarts it: no extra "started".
        board.play(id);
        QCOMPARE(playing.size(), 1);
        rig.lane->pull(kRate);
        QCOMPARE(playing.size(), 2);
        QVERIFY(!playing.last().at(1).toBool());
        QVERIFY(!board.isPlaying(id));
    }

    void gainIsApplied()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        const QString id = board.addFile(rig.writeWav(QStringLiteral("beep.wav")));
        Sound s = board.sounds().first();
        s.gain = 0.5f;
        board.update(s);
        board.play(id);
        const QVector<float> out = rig.lane->pull(kRate / 10);
        QVERIFY(std::fabs(AudioConvert::peak(out.constData(), out.size()) - 0.25f) < 0.01f);
    }

    void stopAndStopAll()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        const QString a = board.addFile(rig.writeWav(QStringLiteral("a.wav"), 1.0));
        const QString b = board.addFile(rig.writeWav(QStringLiteral("b.wav"), 1.0));
        QSignalSpy playing(&board, &Soundboard::playingChanged);
        board.play(a);
        board.play(b);
        board.stop(a);
        QVERIFY(!board.isPlaying(a));
        QVERIFY(board.isPlaying(b));
        QCOMPARE(playing.size(), 3);
        QCOMPARE(playing.last().at(0).toString(), a);
        QVERIFY(!playing.last().at(1).toBool());
        board.stopAll();
        QVERIFY(!board.isPlaying(b));
        QCOMPARE(playing.size(), 4);
        rig.lane->pull(kRate / 50);
        QCOMPARE(AudioConvert::peak(rig.lane->pull(kRate / 10).constData(), kRate / 10), 0.0f);
    }

    void removeDeletesTheCopy()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        const QString source = rig.writeWav(QStringLiteral("clap.wav"));
        const QString id = board.addFile(source);
        const QString copy = board.sounds().first().file;
        board.play(id);
        QSignalSpy playing(&board, &Soundboard::playingChanged);
        board.remove(id);
        QCOMPARE(playing.size(), 1); // stopped first
        QVERIFY(board.sounds().isEmpty());
        QVERIFY(!QFileInfo::exists(copy));
        QVERIFY(QFileInfo::exists(source)); // the original is never touched
        Soundboard again(&rig.player, rig.storeDir);
        QVERIFY(again.load());
        QVERIFY(again.sounds().isEmpty());
        board.remove(QStringLiteral("nope")); // harmless
    }

    void injectedAudioSkipsDecoding()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        // Not decodable at all; the injected PCM is what plays.
        const QString id = board.addFile(rig.writeFile(QStringLiteral("song.mp3"), QByteArray(64, 'x')));
        QVERIFY(!id.isEmpty());
        board.setDecodedAudio(id, QVector<float>(kRate / 10, 0.3f), kRate);
        QSignalSpy errors(&board, &Soundboard::errorOccurred);
        board.play(id);
        QVERIFY(board.isPlaying(id));
        const QVector<float> out = rig.lane->pull(100);
        QVERIFY(std::fabs(out[50] - 0.3f) < 1e-5f);
        QVERIFY(errors.isEmpty());
    }

    void missingFileIsReported()
    {
        Rig rig;
        Soundboard board(&rig.player, rig.storeDir);
        const QString id = board.addFile(rig.writeWav(QStringLiteral("gone.wav")));
        QFile::remove(board.sounds().first().file);
        QSignalSpy errors(&board, &Soundboard::errorOccurred);
        QSignalSpy playing(&board, &Soundboard::playingChanged);
        board.play(id);
        QCOMPARE(errors.size(), 1);
        QVERIFY2(errors.first().at(0).toString().startsWith(QLatin1String("Couldn't read gone.wav")),
                 qPrintable(errors.first().at(0).toString()));
        QVERIFY(playing.isEmpty());
    }

    void decodeWavHelper()
    {
        QVector<float> mono;
        int rate = 0;
        QString error;
        const QVector<float> tone = TestUtil::sine(300, 16000, 0.1);
        QVERIFY(Soundboard::decodeWav(AudioConvert::makeWav16(tone, 16000), &mono, &rate, &error));
        QCOMPARE(rate, 16000);
        QCOMPARE(mono.size(), tone.size());
        QVERIFY(std::fabs(mono[100] - tone[100]) < 1e-3f);
        QVERIFY(!Soundboard::decodeWav(QByteArray("RIFF....WAVEjunk"), &mono, &rate, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Soundboard::decodeWav(QByteArray("ID3 mp3 data"), &mono, &rate, &error));
    }
};

QTEST_GUILESS_MAIN(TestSoundboard)
#include "test_soundboard.moc"
