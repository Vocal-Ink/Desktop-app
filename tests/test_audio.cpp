#include "audio/AudioConvert.h"
#include "audio/Resampler.h"
#include "support/TestUtil.h"

#include <QTest>

class TestAudio : public QObject
{
    Q_OBJECT
private slots:
    void int16RoundTrip()
    {
        const QVector<float> in{0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
        const QAudioFormat fmt = AudioConvert::int16Mono(16000);
        const QByteArray pcm = AudioConvert::fromMonoFloat(in.constData(), in.size(), fmt);
        QCOMPARE(pcm.size(), in.size() * 2);
        const QVector<float> out = AudioConvert::toMonoFloat(pcm, fmt);
        QCOMPARE(out.size(), in.size());
        for (int i = 0; i < in.size(); ++i)
            QVERIFY(std::fabs(out[i] - in[i]) < 1e-3f);
    }

    void stereoIsAveragedAndDuplicated()
    {
        QAudioFormat stereo;
        stereo.setSampleRate(48000);
        stereo.setChannelCount(2);
        stereo.setSampleFormat(QAudioFormat::Float);
        const QVector<float> mono{0.25f, -0.25f};
        const QByteArray pcm = AudioConvert::fromMonoFloat(mono.constData(), mono.size(), stereo, 2.0f);
        QCOMPARE(pcm.size(), 2 * 2 * 4);
        const QVector<float> back = AudioConvert::toMonoFloat(pcm, stereo);
        QCOMPARE(back.size(), 2);
        QVERIFY(std::fabs(back[0] - 0.5f) < 1e-6f);
        QVERIFY(std::fabs(back[1] + 0.5f) < 1e-6f);
    }

    void clipsInsteadOfWrapping()
    {
        const QVector<float> loud{3.0f, -3.0f};
        const QAudioFormat fmt = AudioConvert::int16Mono(8000);
        const QVector<float> back = AudioConvert::toMonoFloat(AudioConvert::fromMonoFloat(loud.constData(), 2, fmt), fmt);
        QVERIFY(back[0] > 0.99f);
        QVERIFY(back[1] < -0.99f);
    }

    void wavRoundTrip()
    {
        const QVector<float> tone = TestUtil::sine(440, 22050, 0.1);
        const QByteArray wav = AudioConvert::makeWav16(tone, 22050);
        const AudioConvert::WavHeader h = AudioConvert::parseWavHeader(wav);
        QVERIFY(h.valid);
        QCOMPARE(h.format.sampleRate(), 22050);
        QCOMPARE(h.format.channelCount(), 1);
        QCOMPARE(h.format.sampleFormat(), QAudioFormat::Int16);
        QCOMPARE(h.dataOffset, qsizetype(44));
        QCOMPARE(h.dataSize, qint64(tone.size() * 2));
    }

    void wavHeaderNeedsMoreData()
    {
        const QByteArray wav = AudioConvert::makeWav16(TestUtil::sine(440, 16000, 0.01), 16000);
        const AudioConvert::WavHeader partial = AudioConvert::parseWavHeader(wav.left(30));
        QVERIFY(!partial.valid);
        QVERIFY(partial.needMoreData);
        QVERIFY(!AudioConvert::parseWavHeader(QByteArray("not a wav file at all")).valid);
    }

    void streamedWavWithUnknownSize()
    {
        QByteArray wav = AudioConvert::makeWav16(TestUtil::sine(440, 16000, 0.01), 16000);
        // Streaming encoders often write 0xFFFFFFFF as the data size.
        wav[40] = char(0xFF);
        wav[41] = char(0xFF);
        wav[42] = char(0xFF);
        wav[43] = char(0xFF);
        const AudioConvert::WavHeader h = AudioConvert::parseWavHeader(wav);
        QVERIFY(h.valid);
        QCOMPARE(h.dataSize, qint64(-1));
    }

    void rmsAndPeak()
    {
        const QVector<float> s = TestUtil::sine(100, 8000, 1.0, 1.0f);
        QVERIFY(std::fabs(AudioConvert::rms(s.constData(), s.size()) - 0.7071f) < 0.01f);
        QVERIFY(AudioConvert::peak(s.constData(), s.size()) > 0.99f);
    }

    void resamplerKeepsPitchAndLength_data()
    {
        QTest::addColumn<int>("inRate");
        QTest::addColumn<int>("outRate");
        QTest::newRow("22050->48000") << 22050 << 48000;
        QTest::newRow("24000->44100") << 24000 << 44100;
        QTest::newRow("48000->16000") << 48000 << 16000;
        QTest::newRow("44100->16000") << 44100 << 16000;
        QTest::newRow("16000->16000") << 16000 << 16000;
    }

    void resamplerKeepsPitchAndLength()
    {
        QFETCH(int, inRate);
        QFETCH(int, outRate);
        const QVector<float> in = TestUtil::sine(440, inRate, 1.0);
        const QVector<float> out = Resampler::convert(in, inRate, outRate);
        QVERIFY(std::abs(out.size() - outRate) <= 2);
        const double f = TestUtil::zeroCrossingFrequency(out.mid(200, out.size() - 400), outRate);
        QVERIFY2(std::fabs(f - 440.0) < 5.0, qPrintable(QString::number(f)));
        // Amplitude preserved (low-pass must not attenuate the pass band).
        const float r = AudioConvert::rms(out.constData() + 200, out.size() - 400);
        QVERIFY2(std::fabs(r - 0.3536f) < 0.02f, qPrintable(QString::number(r)));
    }

    void resamplerRemovesAliasing()
    {
        // 12 kHz is above the 8 kHz output Nyquist frequency and must be strongly attenuated.
        const QVector<float> in = TestUtil::sine(12000, 48000, 0.5);
        const QVector<float> out = Resampler::convert(in, 48000, 16000);
        const float r = AudioConvert::rms(out.constData() + 100, out.size() - 200);
        QVERIFY2(r < 0.05f, qPrintable(QString::number(r)));
    }

    void streamingMatchesOneShot()
    {
        const QVector<float> in = TestUtil::sine(300, 22050, 0.5);
        const QVector<float> oneShot = Resampler::convert(in, 22050, 48000);

        Resampler r(22050, 48000);
        QVector<float> streamed;
        for (qsizetype pos = 0; pos < in.size(); pos += 777)
            streamed += r.process(in.constData() + pos, std::min<qsizetype>(777, in.size() - pos));
        streamed += r.flush();
        streamed.resize(oneShot.size());
        for (qsizetype i = 0; i < oneShot.size(); ++i)
            QVERIFY(std::fabs(oneShot[i] - streamed[i]) < 1e-5f);
    }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "test_audio.moc"
