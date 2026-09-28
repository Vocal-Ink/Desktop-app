# VocalInkAudio: Vocal Ink's Windows virtual mic driver

A WaveRT audio driver with two endpoints on one ROOT-enumerated device
(`ROOT\VocalInkAudio`, service `VocalInkAudio`):

| Endpoint | Direction | What it is for |
|---|---|---|
| **Vocal Ink Voice** | render (speaker) | Vocal Ink plays the voice into it |
| **Vocal Ink Mic** | capture (microphone) | Discord, OBS, Zoom, games record it |

Whatever is played into *Vocal Ink Voice* comes out of *Vocal Ink Mic*
(`Source/Utilities/LoopbackBuffer.*`): 48 kHz, 16-bit or 32-bit float, stereo
(capture also mono). When nothing plays, the mic is silent. Copy-protected
streams are never passed through.

## Where it comes from

Derived from Microsoft's
[SimpleAudioSample](https://github.com/microsoft/Windows-driver-samples/tree/main/audio/simpleaudiosample)
(MS-PL, see `LICENSE`). The git history shows the import, a mechanical rename,
and then every functional change, so the diff against the sample is easy to
review. The main changes: the capture endpoint reads the loopback instead of
generating a sine tone, the render endpoint writes into it instead of
discarding (or saving to a file), both endpoints offer 16-bit and float
formats, the endpoints are named through `MediaCategories` in the INF, the
microphone-array properties and the DRM signature attributes are gone, and
the INF targets Windows 10 2004 (`NT$ARCH$.10.0...19041`) and later.

## Build

Needs Visual Studio 2022 with the C++ workload (Spectre-mitigated libraries
included) and the WDK, which the NuGet packages provide:

```powershell
nuget restore driver\windows\packages.config -PackagesDirectory driver\windows\packages
msbuild driver\windows\VocalInkAudio.sln /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off
```

The package (`VocalInkAudio.sys`, `.inf`, `.cat`) ends up in
`driver\windows\x64\Release\package`. CI (`.github/workflows/drivers.yml`)
builds it, runs `InfVerif /w`, test-signs it and uploads it.

## Signing and installing

Windows with Secure Boot only loads kernel drivers signed by Microsoft. For
releases the package goes through attestation signing (see
`docs/VIRTUAL_AUDIO.md`); the Microsoft-signed files are put in
`packaging/windows/driver/`, where the installer and the app pick them up.

For development, the CI artifact is signed with a self-signed test
certificate. On a test machine: `bcdedit /set testsigning on`, reboot, import
`VocalInkAudioTest.cer` into *Trusted Root Certification Authorities* and
*Trusted Publishers* (Local Machine), then, as administrator:

```powershell
nefconw.exe install VocalInkAudio.inf ROOT\VocalInkAudio --no-duplicates
# remove again:
nefconw.exe --remove-device-node --hardware-id ROOT\VocalInkAudio --class-guid 4d36e96c-e325-11ce-bfc1-08002be10318
```
