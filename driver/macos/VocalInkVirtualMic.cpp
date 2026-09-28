// Copyright (c) Vocal Ink contributors
// SPDX-License-Identifier: Apache-2.0
//
// VocalInkVirtualMic.driver: a Core Audio HAL plug-in (AudioServerPlugIn) built on
// libASPL. It publishes one device, "Vocal Ink Virtual Mic", with an output stream
// and an input stream. Whatever an app plays into the output (Vocal Ink's voice)
// comes out of the input, so Discord, OBS, Zoom or a game can use it as a
// microphone.
//
// coreaudiod loads the bundle from /Library/Audio/Plug-Ins/HAL and calls
// VocalInkVirtualMicEntryPoint (named in Info.plist under CFPlugInFactories).

#include "LoopbackRing.h"

#include <aspl/Driver.hpp>

#include <CoreAudio/AudioServerPlugIn.h>

#include <cmath>
#include <memory>

namespace {

// Never change the UID: macOS remembers per-device settings (and apps remember the
// chosen microphone) by it.
constexpr const char *DeviceUID = "VocalInkVirtualMic_UID";
constexpr const char *ModelUID = "VocalInkVirtualMic_Model";
constexpr const char *DeviceName = "Vocal Ink Virtual Mic"; // src/platform/VirtualDriverDetail.h
constexpr const char *Manufacturer = "Vocal Ink";

constexpr UInt32 SampleRate = 48000;
constexpr UInt32 ChannelCount = vocalink::LoopbackRing::Channels;

static_assert(ChannelCount == 2, "the stream format below is stereo");

// Float32, 48 kHz, stereo, interleaved: the HAL converts to and from whatever the
// apps use. (libASPL's default stream format would be 16-bit integers.)
AudioStreamBasicDescription streamFormat()
{
    AudioStreamBasicDescription f = {};
    f.mSampleRate = SampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f.mBitsPerChannel = 32;
    f.mChannelsPerFrame = ChannelCount;
    f.mBytesPerFrame = sizeof(Float32) * ChannelCount;
    f.mFramesPerPacket = 1;
    f.mBytesPerPacket = f.mBytesPerFrame;
    return f;
}

std::int64_t toSampleTime(Float64 t)
{
    return static_cast<std::int64_t>(std::llround(t));
}

// Handles IO for the device: the mixed output of all apps goes into the ring at the
// output sample time; each app reading the input gets the ring at the input time.
class LoopbackHandler : public aspl::ControlRequestHandler, public aspl::IORequestHandler
{
public:
    OSStatus OnStartIO() override
    {
        // libASPL restarts the sample clock from zero when IO starts.
        m_ring.reset();
        return kAudioHardwareNoError;
    }

    void OnStopIO() override
    {
        m_ring.reset();
    }

    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream> &, Float64, Float64 timestamp, const void *bytes,
                            UInt32 bytesCount) override
    {
        const auto frames = bytesCount / static_cast<UInt32>(sizeof(Float32) * ChannelCount);
        m_ring.write(toSampleTime(timestamp), static_cast<const Float32 *>(bytes), frames);
    }

    void OnReadClientInput(const std::shared_ptr<aspl::Client> &, const std::shared_ptr<aspl::Stream> &, Float64,
                           Float64 timestamp, void *bytes, UInt32 bytesCount) override
    {
        const auto frames = bytesCount / static_cast<UInt32>(sizeof(Float32) * ChannelCount);
        m_ring.read(toSampleTime(timestamp), static_cast<Float32 *>(bytes), frames);
    }

private:
    vocalink::LoopbackRing m_ring;
};

std::shared_ptr<aspl::Driver> createDriver()
{
    // No tracing: coreaudiod would otherwise log every property request to syslog.
    auto context = std::make_shared<aspl::Context>(std::make_shared<aspl::Tracer>(aspl::Tracer::Mode::Noop));

    aspl::DeviceParameters deviceParams;
    deviceParams.Name = DeviceName;
    deviceParams.Manufacturer = Manufacturer;
    deviceParams.DeviceUID = DeviceUID;
    deviceParams.ModelUID = ModelUID;
    deviceParams.SampleRate = SampleRate;
    deviceParams.ChannelCount = ChannelCount;
    deviceParams.Latency = 0;
    deviceParams.SafetyOffset = 0;
    // Frames between zero timestamps. The default (one second) makes the HAL add a lot
    // of latency; 16384 matches the common loopback drivers and still exceeds any IO
    // buffer size the HAL picks.
    deviceParams.ZeroTimeStampPeriod = 16384;
    // It may be chosen as the system microphone (web apps use the default input), but
    // alerts and beeps should never be sent into it.
    deviceParams.CanBeDefault = true;
    deviceParams.CanBeDefaultForSystemSounds = false;
    deviceParams.EnableMixing = true;

    auto device = std::make_shared<aspl::Device>(context, deviceParams);

    aspl::StreamParameters output;
    output.Direction = aspl::Direction::Output;
    output.Format = streamFormat();
    device->AddStreamWithControlsAsync(output);

    aspl::StreamParameters input;
    input.Direction = aspl::Direction::Input;
    input.Format = streamFormat();
    device->AddStreamWithControlsAsync(input);

    auto handler = std::make_shared<LoopbackHandler>();
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);

    aspl::PluginParameters pluginParams;
    pluginParams.Manufacturer = Manufacturer;
    auto plugin = std::make_shared<aspl::Plugin>(context, pluginParams);
    plugin->AddDevice(device);

    return std::make_shared<aspl::Driver>(context, plugin);
}

} // namespace

// The only exported symbol (see CMakeLists.txt).
extern "C" __attribute__((visibility("default"))) void *VocalInkVirtualMicEntryPoint(CFAllocatorRef allocator,
                                                                                    CFUUIDRef requestedTypeUUID)
{
    (void)allocator;
    // 443ABAB8-E7B3-491A-B985-BEB9187030DB: the AudioServerPlugIn type.
    if (!CFEqual(requestedTypeUUID, kAudioServerPlugInTypeUUID))
        return nullptr;
    static std::shared_ptr<aspl::Driver> driver = createDriver();
    return driver->GetReference();
}
