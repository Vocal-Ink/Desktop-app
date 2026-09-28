// The macOS HAL plug-in's loopback ring is plain C++, so it is tested here on every OS.
#include "../driver/macos/LoopbackRing.h"

#include <QTest>

#include <vector>

using vocalink::LoopbackRing;

namespace {

constexpr std::uint32_t Ch = LoopbackRing::Channels;

// Frames whose left sample is the sample time and right sample its negative.
std::vector<float> ramp(std::int64_t start, std::uint32_t frames)
{
    std::vector<float> v(std::size_t(frames) * Ch);
    for (std::uint32_t i = 0; i < frames; ++i) {
        v[i * Ch] = float(start + i);
        v[i * Ch + 1] = -float(start + i);
    }
    return v;
}

std::vector<float> readAt(const LoopbackRing &ring, std::int64_t start, std::uint32_t frames)
{
    std::vector<float> v(std::size_t(frames) * Ch, 123.0f);
    ring.read(start, v.data(), frames);
    return v;
}

bool isSilent(const std::vector<float> &v, std::size_t fromFrame = 0, std::size_t toFrame = SIZE_MAX)
{
    toFrame = std::min(toFrame, v.size() / Ch);
    for (std::size_t i = fromFrame * Ch; i < toFrame * Ch; ++i) {
        if (v[i] != 0.0f)
            return false;
    }
    return true;
}

bool matchesRamp(const std::vector<float> &v, std::int64_t start, std::size_t fromFrame, std::size_t toFrame)
{
    for (std::size_t i = fromFrame; i < toFrame; ++i) {
        if (v[i * Ch] != float(start + std::int64_t(i)) || v[i * Ch + 1] != -float(start + std::int64_t(i)))
            return false;
    }
    return true;
}

} // namespace

class TestLoopbackRing : public QObject
{
    Q_OBJECT
private slots:
    void silentBeforeAnythingIsPlayed()
    {
        LoopbackRing ring;
        QVERIFY(isSilent(readAt(ring, 0, 512)));
        QVERIFY(isSilent(readAt(ring, 100000, 512)));
    }

    void readsBackWhatWasPlayedAtTheSameTime()
    {
        LoopbackRing ring;
        // Output runs ahead of input: write cycles at 1024.., read the same times later.
        for (std::int64_t t = 1024; t < 1024 + 512 * 8; t += 512) {
            const auto frames = ramp(t, 512);
            ring.write(t, frames.data(), 512);
        }
        const auto got = readAt(ring, 1024 + 512 * 3, 512);
        QVERIFY(matchesRamp(got, 1024 + 512 * 3, 0, 512));
    }

    void partlyWrittenRangesArePaddedWithSilence()
    {
        LoopbackRing ring;
        const auto frames = ramp(1000, 100);
        ring.write(1000, frames.data(), 100);
        const auto got = readAt(ring, 950, 200); // 50 before, 100 written, 50 after
        QVERIFY(isSilent(got, 0, 50));
        QVERIFY(matchesRamp(got, 950, 50, 150));
        QVERIFY(isSilent(got, 150, 200));
    }

    void silenceAfterPlaybackStops()
    {
        LoopbackRing ring;
        const auto frames = ramp(0, 4096);
        ring.write(0, frames.data(), 4096);
        QVERIFY(isSilent(readAt(ring, 4096, 512)));
        QVERIFY(isSilent(readAt(ring, 70000, 512)));
    }

    void neverReplaysAudioFromBeforeAGap()
    {
        LoopbackRing ring;
        // First run fills the whole ring.
        for (std::int64_t t = 0; t < LoopbackRing::Frames; t += 1024) {
            const auto frames = ramp(t, 1024);
            ring.write(t, frames.data(), 1024);
        }
        // Playback pauses, then resumes exactly one ring length later, so the slots
        // for the next reads still hold the old run.
        const std::int64_t resume = LoopbackRing::Frames * 2;
        const auto frames = ramp(resume, 512);
        ring.write(resume, frames.data(), 512);
        // Reading just before the new run must not return the stale samples in those slots.
        QVERIFY(isSilent(readAt(ring, resume - 512, 512)));
        QVERIFY(matchesRamp(readAt(ring, resume, 512), resume, 0, 512));
    }

    void overwrittenSamplesAreGone()
    {
        LoopbackRing ring;
        std::int64_t t = 0;
        for (; t < LoopbackRing::Frames + 4096; t += 1024) {
            const auto frames = ramp(t, 1024);
            ring.write(t, frames.data(), 1024);
        }
        // The first 4096 frames were overwritten by the last 4096.
        QVERIFY(isSilent(readAt(ring, 0, 4096)));
        QVERIFY(matchesRamp(readAt(ring, t - 1024, 1024), t - 1024, 0, 1024));
        QVERIFY(matchesRamp(readAt(ring, 4096, 1024), 4096, 0, 1024));
    }

    void oversizedWritesKeepTheNewestFrames()
    {
        LoopbackRing ring;
        const std::uint32_t n = LoopbackRing::Frames + 100;
        const auto frames = ramp(0, n);
        ring.write(0, frames.data(), n);
        QVERIFY(isSilent(readAt(ring, 0, 100)));
        QVERIFY(matchesRamp(readAt(ring, 100, 512), 100, 0, 512));
        QVERIFY(matchesRamp(readAt(ring, n - 512, 512), n - 512, 0, 512));
    }

    void resetForgetsEverything()
    {
        LoopbackRing ring;
        const auto frames = ramp(0, 1024);
        ring.write(0, frames.data(), 1024);
        ring.reset();
        QVERIFY(isSilent(readAt(ring, 0, 1024)));
    }

    void jumpingBackStartsANewRun()
    {
        LoopbackRing ring;
        const auto first = ramp(10000, 1024);
        ring.write(10000, first.data(), 1024);
        const auto second = ramp(0, 512); // sample times restarted without a reset
        ring.write(0, second.data(), 512);
        QVERIFY(matchesRamp(readAt(ring, 0, 512), 0, 0, 512));
        QVERIFY(isSilent(readAt(ring, 10000, 1024)));
    }
};

QTEST_GUILESS_MAIN(TestLoopbackRing)
#include "test_loopbackring.moc"
