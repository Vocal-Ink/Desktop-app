/*++

Copyright (c) Vocal Ink contributors.
Licensed under the Microsoft Public License (MS-PL), see LICENSE in driver/windows.

Module Name:

    LoopbackBuffer.h

Abstract:

    The loopback between the two endpoints of the Vocal Ink virtual audio device.
    Whatever is played into the "Vocal Ink Voice" render endpoint can be recorded
    from the "Vocal Ink Mic" capture endpoint.

    The render stream writes into one ring (48 kHz, stereo, 32-bit float) as its
    simulated DMA position moves; each capture stream is a reader with its own
    position. A reader starts at the newest frame (it never hears audio from
    before it started), waits until a little audio is buffered to absorb timer
    jitter, and returns silence whenever the render side has nothing new, so
    stopped playback turns into silence and nothing is ever replayed.

    One adapter owns one CLoopbackBuffer (see CAdapterCommon::GetLoopbackBuffer).
--*/

#ifndef _VOCALINKAUDIO_LOOPBACKBUFFER_H_
#define _VOCALINKAUDIO_LOOPBACKBUFFER_H_

#define LOOPBACK_SAMPLE_RATE        48000
#define LOOPBACK_CHANNELS           2
#define LOOPBACK_BUFFER_FRAMES      (LOOPBACK_SAMPLE_RATE / 2)      // 500 ms ring
#define LOOPBACK_TARGET_FRAMES      (LOOPBACK_SAMPLE_RATE / 50)     // 20 ms buffered before a reader starts
#define LOOPBACK_MAX_LAG_FRAMES     (LOOPBACK_SAMPLE_RATE / 5)      // a reader never falls more than 200 ms behind

C_ASSERT(LOOPBACK_TARGET_FRAMES < LOOPBACK_MAX_LAG_FRAMES);
C_ASSERT(LOOPBACK_MAX_LAG_FRAMES < LOOPBACK_BUFFER_FRAMES / 2);

//
// Per capture stream state.
//
typedef struct _LOOPBACK_READER
{
    ULONGLONG   Frame;      // next frame to read, counted like CLoopbackBuffer's write position
    BOOLEAN     Primed;     // enough audio was buffered to start playing it out
} LOOPBACK_READER, *PLOOPBACK_READER;

///////////////////////////////////////////////////////////////////////////////
// CLoopbackBuffer
//
class CLoopbackBuffer
{
public:
    CLoopbackBuffer();
    ~CLoopbackBuffer();

    // Allocates the ring (non-paged). PASSIVE_LEVEL.
    NTSTATUS Init();

    // 48 kHz, 1 or 2 channels, 16-bit PCM or 32-bit float.
    static BOOL IsFormatSupported
    (
        _In_ const WAVEFORMATEX* Format
    );

    // Start a capture stream at "now". <= DISPATCH_LEVEL.
    VOID ResetReader
    (
        _Out_ PLOOPBACK_READER Reader
    );

    // Render side: appends whole frames in the render stream's format. When
    // Silence is TRUE (copy-protected content), zeros are written instead.
    // <= DISPATCH_LEVEL.
    VOID Write
    (
        _In_reads_bytes_(ByteCount) const BYTE*     Data,
        _In_                        ULONG           ByteCount,
        _In_                        const WAVEFORMATEX* Format,
        _In_                        BOOL            Silence
    );

    // Capture side: fills ByteCount bytes in the capture stream's format.
    // <= DISPATCH_LEVEL.
    VOID Read
    (
        _Out_writes_bytes_(ByteCount)   BYTE*               Data,
        _In_                            ULONG               ByteCount,
        _In_                            const WAVEFORMATEX* Format,
        _Inout_                         PLOOPBACK_READER    Reader
    );

private:
    KSPIN_LOCK      m_Lock;
    float*          m_Samples;      // LOOPBACK_BUFFER_FRAMES * LOOPBACK_CHANNELS, interleaved
    ULONGLONG       m_WriteFrame;   // total frames written since the adapter started
};

typedef CLoopbackBuffer *PLOOPBACKBUFFER;

#endif // _VOCALINKAUDIO_LOOPBACKBUFFER_H_
