// Copyright (c) Vocal Ink contributors
// SPDX-License-Identifier: Apache-2.0
//
// The loopback at the heart of VocalInkVirtualMic.driver: what apps play into the
// device's output stream comes back out of its input stream.
//
// Samples are stored by the device's sample time: output writes at the output
// time of each IO cycle, input reads at the (earlier) input time, so a read picks
// up exactly what was played for that moment. Only the current unbroken run of
// writes counts: when playback stops, reads turn into silence, and when it starts
// again nothing from before the gap can be replayed.
//
// Plain C++17 with no Core Audio dependency, so it is unit-tested on every OS
// (tests/test_loopbackring.cpp). The HAL calls write() and read() for one device on
// its IO thread; the positions are atomics so a stray call from elsewhere can't
// see a torn value.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace vocalink {

class LoopbackRing
{
public:
    static constexpr std::uint32_t Frames = 65536; // ~1.4 s at 48 kHz
    static constexpr std::uint32_t Channels = 2;

    LoopbackRing()
        : m_samples(std::size_t(Frames) * Channels, 0.0f)
    {
    }

    // Forget everything written so far (IO restarted, sample times start over).
    void reset()
    {
        m_hasRun.store(false, std::memory_order_release);
        m_runStart.store(0, std::memory_order_relaxed);
        m_writeEnd.store(0, std::memory_order_relaxed);
    }

    // Stores `frames` interleaved frames that play at `sampleTime`.
    void write(std::int64_t sampleTime, const float *src, std::uint32_t frames)
    {
        if (frames == 0)
            return;
        if (frames > Frames) { // only the newest Frames frames fit
            src += std::size_t(frames - Frames) * Channels;
            sampleTime += frames - Frames;
            frames = Frames;
        }
        const bool continues = m_hasRun.load(std::memory_order_acquire)
                               && sampleTime == m_writeEnd.load(std::memory_order_relaxed);
        if (!continues) {
            // A gap (or a jump back) starts a new run; older samples must never be read.
            m_hasRun.store(false, std::memory_order_release);
            m_runStart.store(sampleTime, std::memory_order_relaxed);
        }
        for (std::uint32_t i = 0; i < frames; ++i) {
            const std::size_t slot = index(sampleTime + i) * Channels;
            for (std::uint32_t c = 0; c < Channels; ++c)
                m_samples[slot + c] = src[std::size_t(i) * Channels + c];
        }
        m_writeEnd.store(sampleTime + frames, std::memory_order_relaxed);
        m_hasRun.store(true, std::memory_order_release);
    }

    // Fills `frames` interleaved frames recorded at `sampleTime`. Whatever wasn't
    // written in the current run (or was overwritten since) is silence.
    void read(std::int64_t sampleTime, float *dst, std::uint32_t frames) const
    {
        std::fill(dst, dst + std::size_t(frames) * Channels, 0.0f);
        if (frames == 0 || !m_hasRun.load(std::memory_order_acquire))
            return;
        const std::int64_t writeEnd = m_writeEnd.load(std::memory_order_relaxed);
        const std::int64_t validStart = std::max(m_runStart.load(std::memory_order_relaxed),
                                                 writeEnd - std::int64_t(Frames));
        const std::int64_t from = std::max(sampleTime, validStart);
        const std::int64_t to = std::min(sampleTime + std::int64_t(frames), writeEnd);
        for (std::int64_t t = from; t < to; ++t) {
            const std::size_t slot = index(t) * Channels;
            const std::size_t out = std::size_t(t - sampleTime) * Channels;
            for (std::uint32_t c = 0; c < Channels; ++c)
                dst[out + c] = m_samples[slot + c];
        }
    }

private:
    static std::size_t index(std::int64_t sampleTime)
    {
        const std::int64_t m = sampleTime % std::int64_t(Frames);
        return std::size_t(m < 0 ? m + Frames : m);
    }

    std::vector<float> m_samples;
    std::atomic<bool> m_hasRun{false};
    std::atomic<std::int64_t> m_runStart{0}; // first sample time of the current run
    std::atomic<std::int64_t> m_writeEnd{0}; // one past the last written sample time
};

} // namespace vocalink
