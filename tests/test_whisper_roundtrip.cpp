#include "audio/AudioConvert.h"
#include "audio/Resampler.h"
#include "core/Settings.h"
#include "stt/WhisperEngine.h"
#include "support/TestUtil.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {
QString modelFromEnvironment()
{
    const QString model = qEnvironmentVariable("VOCALINK_WHISPER_MODEL");
    return QFileInfo(model).isFile() ? model : QString();
}
} // namespace

// End-to-end check of on-device recognition: espeak-ng speaks a sentence, Whisper
// writes it down. Needs a real model, so it only runs when VOCALINK_WHISPER_MODEL
// points to a ggml file (CI uses ggml-tiny.en).
class TestWhisperRoundtrip : public QObject
{
    Q_OBJECT
private slots:
    void transcribesSynthesizedSpeech()
    {
#ifndef VOCALINK_HAVE_WHISPER
        QSKIP("Built without whisper.cpp");
#else
        const QString model = modelFromEnvironment();
        if (model.isEmpty())
            QSKIP("Set VOCALINK_WHISPER_MODEL to a ggml Whisper model to run this test");
        const QString espeak = QStandardPaths::findExecutable(QStringLiteral("espeak-ng"));
        if (espeak.isEmpty())
            QSKIP("espeak-ng is not installed");

        QProcess speak;
        speak.start(espeak, {QStringLiteral("--stdout"), QStringLiteral("hello world, this is a test")});
        QVERIFY(speak.waitForFinished(30000));
        QCOMPARE(speak.exitCode(), 0);
        const QByteArray wav = speak.readAllStandardOutput();
        const AudioConvert::WavHeader header = AudioConvert::parseWavHeader(wav);
        QVERIFY(header.valid);
        const QByteArray pcm = header.dataSize >= 0 ? wav.mid(header.dataOffset, header.dataSize) : wav.mid(header.dataOffset);
        const QVector<float> mono = AudioConvert::toMonoFloat(pcm, header.format);
        QVERIFY(mono.size() > header.format.sampleRate() / 2);
        const QVector<float> audio = Resampler::convert(mono, header.format.sampleRate(), 16000);

        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        WhisperEngine engine(EngineContext{nullptr, &settings, nullptr});
        QSignalSpy loadFailed(&engine, &WhisperEngine::modelLoadFailed);
        engine.setModelPathForTesting(model);
        QTRY_VERIFY_WITH_TIMEOUT(engine.isReady() || !loadFailed.isEmpty(), 60000);
        QVERIFY2(engine.isReady(), qPrintable(engine.notReadyReason()));

        SttEngine::Options options;
        options.language = QStringLiteral("en");
        engine.setOptions(options);
        QSignalSpy done(&engine, &SttEngine::transcribed);
        QSignalSpy failed(&engine, &SttEngine::failed);
        engine.transcribe(42, audio);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + failed.count() > 0, 90000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(done.first().at(0).toULongLong(), quint64(42));
        const QString text = done.first().at(1).toString();
        qInfo("Whisper heard: %s", qPrintable(text));
        // eSpeak through the tiny model is rough ("The low world is in the test."):
        // most of the words must come back, not every one.
        const QStringList expected = {QStringLiteral("hello"), QStringLiteral("world"), QStringLiteral("this"),
                                      QStringLiteral("is"), QStringLiteral("test")};
        const QStringList heard = text.toLower().split(QRegularExpression(QStringLiteral("[^a-z]+")), Qt::SkipEmptyParts);
        int matched = 0;
        for (const QString &w : expected)
            matched += heard.contains(w) ? 1 : 0;
        QVERIFY2(matched >= 3, qPrintable(QStringLiteral("only %1 of %2 words recognised in: %3").arg(matched).arg(expected.size()).arg(text)));
#endif
    }

    // Closing the app (or switching models) must not wait for a long transcription.
    // whisper.cpp checks the abort flag between encoder/decoder passes, so the engine
    // may still finish the pass in progress, but never the remaining audio.
    void abortsWhenDestroyedMidTranscription()
    {
#ifndef VOCALINK_HAVE_WHISPER
        QSKIP("Built without whisper.cpp");
#else
        const QString model = modelFromEnvironment();
        if (model.isEmpty())
            QSKIP("Set VOCALINK_WHISPER_MODEL to a ggml Whisper model to run this test");
        QTemporaryDir dir;
        Settings settings(dir.filePath(QStringLiteral("settings.ini")), nullptr);
        auto engine = std::make_unique<WhisperEngine>(EngineContext{nullptr, &settings, nullptr});
        QSignalSpy loadFailed(engine.get(), &WhisperEngine::modelLoadFailed);
        engine->setModelPathForTesting(model);
        QTRY_VERIFY_WITH_TIMEOUT(engine->isReady() || !loadFailed.isEmpty(), 60000);
        QVERIFY2(engine->isReady(), qPrintable(engine->notReadyReason()));

        QSignalSpy done(engine.get(), &SttEngine::transcribed);
        engine->transcribe(1, TestUtil::sine(220, 16000, 90.0, 0.3f));
        QTest::qWait(300);
        QElapsedTimer timer;
        timer.start();
        engine.reset();
        QVERIFY2(timer.elapsed() < 30000, qPrintable(QString::number(timer.elapsed())));
        QTest::qWait(50);
        QCOMPARE(done.count(), 0);
#endif
    }
};

QTEST_GUILESS_MAIN(TestWhisperRoundtrip)
#include "test_whisper_roundtrip.moc"
