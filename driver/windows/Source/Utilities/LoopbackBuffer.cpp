/*++

Copyright (c) Vocal Ink contributors.
Licensed under the Microsoft Public License (MS-PL), see LICENSE in driver/windows.

Module Name:

    LoopbackBuffer.cpp

Abstract:

    Implementation of the render-to-capture loopback. See LoopbackBuffer.h.

--*/

#include "definitions.h"
#include <limits.h>
#include "LoopbackBuffer.h"

#define LOOPBACK_POOLTAG    'BLnV'

namespace
{

enum SAMPLE_KIND
{
    SampleKindUnsupported = 0,
    SampleKindPcm16,
    SampleKindFloat32,
};

//
// Works out the sample encoding of a (possibly extensible) wave format.
//
SAMPLE_KIND GetSampleKind(_In_ const WAVEFORMATEX* Format)
{
    if (Format->nSamplesPerSec != LOOPBACK_SAMPLE_RATE ||
        (Format->nChannels != 1 && Format->nChannels != 2))
    {
        return SampleKindUnsupported;
    }

    BOOL isPcm   = (Format->wFormatTag == WAVE_FORMAT_PCM);
    BOOL isFloat = (Format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        Format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        const WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);
        isPcm   = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    if (isPcm && Format->wBitsPerSample == 16 && Format->nBlockAlign == Format->nChannels * 2)
    {
        return SampleKindPcm16;
    }
    if (isFloat && Format->wBitsPerSample == 32 && Format->nBlockAlign == Format->nChannels * 4)
    {
        return SampleKindFloat32;
    }
    return SampleKindUnsupported;
}

// The callers save the floating point state around these.
#pragma warning(push)
#pragma warning(disable: 28110)

float Pcm16ToFloat(_In_ SHORT Value)
{
    return static_cast<float>(Value) * (1.0f / 32768.0f);
}

SHORT FloatToPcm16(_In_ float Value)
{
    if (Value >= 1.0f)
    {
        return SHRT_MAX;
    }
    if (Value <= -1.0f)
    {
        return SHRT_MIN;
    }
    return static_cast<SHORT>(Value * 32767.0f);
}

#pragma warning(pop)

} // namespace

//=============================================================================
#pragma code_seg("PAGE")
CLoopbackBuffer::CLoopbackBuffer()
    : m_Samples(NULL),
      m_WriteFrame(0)
{
    PAGED_CODE();

    KeInitializeSpinLock(&m_Lock);
}

//=============================================================================
#pragma code_seg("PAGE")
CLoopbackBuffer::~CLoopbackBuffer()
{
    PAGED_CODE();

    if (m_Samples)
    {
        ExFreePoolWithTag(m_Samples, LOOPBACK_POOLTAG);
        m_Samples = NULL;
    }
}

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS CLoopbackBuffer::Init()
{
    PAGED_CODE();

    if (m_Samples != NULL)
    {
        return STATUS_SUCCESS;
    }

    // ExAllocatePool2 zeroes the memory, so the ring starts out silent.
    m_Samples = static_cast<float*>(ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(float) * LOOPBACK_BUFFER_FRAMES * LOOPBACK_CHANNELS,
        LOOPBACK_POOLTAG));

    return m_Samples != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

//=============================================================================
#pragma code_seg()
BOOL CLoopbackBuffer::IsFormatSupported
(
    _In_ const WAVEFORMATEX* Format
)
{
    return GetSampleKind(Format) != SampleKindUnsupported;
}

//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::ResetReader
(
    _Out_ PLOOPBACK_READER Reader
)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);

    Reader->Frame = m_WriteFrame;
    Reader->Primed = FALSE;

    KeReleaseSpinLock(&m_Lock, oldIrql);
}

//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::Write
(
    _In_reads_bytes_(ByteCount) const BYTE*         Data,
    _In_                        ULONG               ByteCount,
    _In_                        const WAVEFORMATEX* Format,
    _In_                        BOOL                Silence
)
{
    const SAMPLE_KIND kind = GetSampleKind(Format);
    if (m_Samples == NULL || kind == SampleKindUnsupported)
    {
        return;
    }

    const ULONG channels = Format->nChannels;
    const ULONG blockAlign = Format->nBlockAlign;
    const ULONG frames = ByteCount / blockAlign;
    // The render stream moves in whole frames at 48 kHz.
    ASSERT(ByteCount % blockAlign == 0);

    KFLOATING_SAVE floatSave;
    if (!NT_SUCCESS(KeSaveFloatingPointState(&floatSave)))
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);

    for (ULONG i = 0; i < frames; ++i)
    {
        const ULONG slot = static_cast<ULONG>(m_WriteFrame % LOOPBACK_BUFFER_FRAMES) * LOOPBACK_CHANNELS;
        float left = 0.0f;
        float right = 0.0f;

        if (!Silence)
        {
            const BYTE* frame = Data + static_cast<SIZE_T>(i) * blockAlign;
            if (kind == SampleKindPcm16)
            {
                const SHORT* samples = reinterpret_cast<const SHORT*>(frame);
                left = Pcm16ToFloat(samples[0]);
                right = (channels > 1) ? Pcm16ToFloat(samples[1]) : left;
            }
            else
            {
                const float* samples = reinterpret_cast<const float*>(frame);
                left = samples[0];
                right = (channels > 1) ? samples[1] : left;
            }
        }

        m_Samples[slot] = left;
        m_Samples[slot + 1] = right;
        ++m_WriteFrame;
    }

    KeReleaseSpinLock(&m_Lock, oldIrql);
    KeRestoreFloatingPointState(&floatSave);
}

//=============================================================================
#pragma code_seg()
VOID CLoopbackBuffer::Read
(
    _Out_writes_bytes_(ByteCount)   BYTE*               Data,
    _In_                            ULONG               ByteCount,
    _In_                            const WAVEFORMATEX* Format,
    _Inout_                         PLOOPBACK_READER    Reader
)
{
    // Start from silence; only frames that were really played get filled in.
    RtlZeroMemory(Data, ByteCount);

    const SAMPLE_KIND kind = GetSampleKind(Format);
    if (m_Samples == NULL || kind == SampleKindUnsupported)
    {
        return;
    }

    const ULONG channels = Format->nChannels;
    const ULONG blockAlign = Format->nBlockAlign;
    const ULONG frames = ByteCount / blockAlign;

    KFLOATING_SAVE floatSave;
    if (!NT_SUCCESS(KeSaveFloatingPointState(&floatSave)))
    {
        return;
    }

    KIRQL oldIrql;
    KeAcquireSpinLock(&m_Lock, &oldIrql);

    if (Reader->Frame > m_WriteFrame)
    {
        // Can't be ahead of the writer; start over.
        Reader->Frame = m_WriteFrame;
        Reader->Primed = FALSE;
    }

    ULONGLONG available = m_WriteFrame - Reader->Frame;

    if (available > LOOPBACK_MAX_LAG_FRAMES)
    {
        // Fell too far behind (the capture stream was starved): keep only the newest audio.
        Reader->Frame = m_WriteFrame - LOOPBACK_TARGET_FRAMES;
        available = LOOPBACK_TARGET_FRAMES;
    }

    if (!Reader->Primed)
    {
        if (available < LOOPBACK_TARGET_FRAMES)
        {
            goto Done;  // still buffering: silence
        }
        Reader->Primed = TRUE;
    }

    {
        const ULONG count = (available < frames) ? static_cast<ULONG>(available) : frames;

        for (ULONG i = 0; i < count; ++i)
        {
            const ULONG slot = static_cast<ULONG>((Reader->Frame + i) % LOOPBACK_BUFFER_FRAMES) * LOOPBACK_CHANNELS;
            const float left = m_Samples[slot];
            const float right = m_Samples[slot + 1];
            BYTE* frame = Data + static_cast<SIZE_T>(i) * blockAlign;

            if (kind == SampleKindPcm16)
            {
                SHORT* samples = reinterpret_cast<SHORT*>(frame);
                if (channels > 1)
                {
                    samples[0] = FloatToPcm16(left);
                    samples[1] = FloatToPcm16(right);
                }
                else
                {
                    samples[0] = FloatToPcm16((left + right) * 0.5f);
                }
            }
            else
            {
                float* samples = reinterpret_cast<float*>(frame);
                if (channels > 1)
                {
                    samples[0] = left;
                    samples[1] = right;
                }
                else
                {
                    samples[0] = (left + right) * 0.5f;
                }
            }
        }

        Reader->Frame += count;

        if (count < frames)
        {
            // Ran dry (playback stopped or stalled): the rest stays silent and the
            // reader buffers up again before it plays anything more.
            Reader->Primed = FALSE;
        }
    }

Done:
    KeReleaseSpinLock(&m_Lock, oldIrql);
    KeRestoreFloatingPointState(&floatSave);
}
