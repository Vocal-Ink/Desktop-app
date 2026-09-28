# Vocal Ink's virtual microphone

Discord, OBS, Zoom and games can only hear Vocal Ink through a *virtual audio
device*: Vocal Ink plays the voice into a virtual output, and the other app
records the matching virtual input as if it were a microphone.

Vocal Ink brings its own virtual mic on every system, so you don't have to find
and install a third-party cable. VB-CABLE (Windows) and BlackHole (macOS) keep
working as fallbacks.

| | Windows | macOS | Linux |
|---|---|---|---|
| Vocal Ink plays into | **Vocal Ink Voice** | **Vocal Ink Virtual Mic** | **Vocal Ink Voice** |
| Pick as the microphone elsewhere | **Vocal Ink Mic** | **Vocal Ink Virtual Mic** | **Vocal Ink Mic** |
| Installed by | the installer (optional task) or Vocal Ink's audio settings | Vocal Ink's audio settings | Vocal Ink's audio settings |
| Asks for | Windows permission (UAC) | your Mac password | nothing |
| Fallback | [VB-CABLE](https://vb-audio.com/Cable/) | [BlackHole 2ch](https://existential.audio/blackhole/) | not needed |

On macOS it is one device that works in both directions, so it has the same
name in the output and input lists.

Keep *Also play on my speakers* on in Vocal Ink if you want to hear yourself:
the virtual mic only carries the voice to other apps.

---

## How it works

### Windows

A small audio driver, `VocalInkAudio.sys` (source in `driver/windows`, derived
from Microsoft's SimpleAudioSample), adds one device, *Vocal Ink Virtual Audio
Device*, with two endpoints:

- **Vocal Ink Voice** (a speaker): Vocal Ink plays into it.
- **Vocal Ink Mic** (a microphone): other apps record it.

Whatever is played into Vocal Ink Voice comes out of Vocal Ink Mic about 20 ms
later. When nothing plays, the mic is silent. Both endpoints use 48 kHz; the
mic offers stereo and mono, 16-bit and 32-bit float. In the Windows sound
settings they appear as *Vocal Ink Voice (Vocal Ink Virtual Audio Device)* and
*Vocal Ink Mic (Vocal Ink Virtual Audio Device)*.

The driver is installed with [nefcon](https://github.com/nefarius/nefcon)
(`nefconw.exe`, shipped in the `driver` folder next to `VocalInk.exe`):

```
nefconw install "<app>\driver\VocalInkAudio.inf" ROOT\VocalInkAudio --no-duplicates
nefconw --remove-device-node --hardware-id ROOT\VocalInkAudio --class-guid 4d36e96c-e325-11ce-bfc1-08002be10318
```

The installer runs the first line when *Install the Vocal Ink virtual
microphone* is ticked (all-users installs only, because drivers need
administrator rights) and the second when Vocal Ink is uninstalled. Vocal Ink
can do the same from its audio settings; Windows then shows one permission
prompt.

Windows only loads drivers signed by Microsoft (see [For
maintainers](#windows-driver-signing)). A build of Vocal Ink that doesn't carry
the signed driver says so and points to VB-CABLE instead.

### macOS

`VocalInkVirtualMic.driver` (source in `driver/macos`) is a Core Audio HAL
plug-in built with [libASPL](https://github.com/gavv/libASPL). It ships inside
the app, in `Vocal Ink.app/Contents/Resources`. When you click Install, Vocal
Ink asks for your password once, copies it to `/Library/Audio/Plug-Ins/HAL`,
and restarts the macOS audio service (`coreaudiod`). Sound stops for a second
while that happens. The device then appears as **Vocal Ink Virtual Mic**; if it
doesn't within ten seconds, Vocal Ink asks you to restart the Mac.

Whatever is played into the device's output comes out of its input
(48 kHz stereo; macOS converts formats for each app). When nothing plays, the
input is silent. Its output and input volume and mute controls in *Audio MIDI
Setup* work.

When a newer Vocal Ink carries a newer driver, the settings offer the update.
Removing it asks for the password again and deletes the plug-in.

### Linux

No driver is needed: PulseAudio and PipeWire can create virtual devices. Vocal
Ink creates a sink called **Vocal Ink Voice** (`vocalink_voice`) and a source
called **Vocal Ink Mic** (`vocalink_mic`) that records it, right away and every
time you log in. No password is needed.

- **PipeWire** (most current distributions): Vocal Ink writes
  `~/.config/pipewire/pipewire.conf.d/60-vocalink-virtual-mic.conf`, a
  `libpipewire-module-loopback` whose input side is the sink and whose output
  side is the source, and creates the same devices for the current session
  with `pactl`.
- **PulseAudio**: Vocal Ink adds a clearly marked block to
  `~/.config/pulse/default.pa` (creating the file with
  `.include /etc/pulse/default.pa` first, so nothing else changes) and loads
  the modules now with `pactl`.

Removing it deletes the file (or the block) and unloads the modules. With
PipeWire, devices created at log-in disappear at the next log-in.

OBS can also record *Monitor of Vocal Ink Voice* directly.

---

## Picking it in apps

Choose **Vocal Ink Voice** (macOS: **Vocal Ink Virtual Mic**) as Vocal Ink's
voice output first. Then, in the other app, choose **Vocal Ink Mic** (macOS:
**Vocal Ink Virtual Mic**) as the microphone:

- **Discord**: *User Settings → Voice & Video → Input Device*. If words get cut
  off, turn off *Noise Suppression* and set *Input Sensitivity* by hand, or use
  Push to Talk bound to the same key you use to speak in Vocal Ink.
- **OBS Studio**: add an *Audio Input Capture* source (macOS: *Audio Input
  Capture*, device *Vocal Ink Virtual Mic*). On Linux you can use *Audio Output
  Capture* of *Vocal Ink Voice* instead.
- **Zoom**: *Settings → Audio → Microphone*. Turn off *Automatically adjust
  microphone volume* and set background noise suppression to *Low*.
- **Microsoft Teams, Google Meet and other browser apps**: pick it in the
  app's or the browser's microphone settings. Browsers on macOS often use the
  system default input; you can make *Vocal Ink Virtual Mic* the default in
  *System Settings → Sound → Input*.
- **Games**: pick it in the game's voice chat settings. Games that always use
  the system's default microphone need it set as the default recording device
  (Windows: *Settings → System → Sound → Input*).

---

## Troubleshooting

- **It doesn't show up after installing.** Windows: restart Windows. macOS:
  restart the Mac. Linux: log out and back in. Then check Vocal Ink's audio
  settings again.
- **Others can't hear anything.** Check that Vocal Ink's voice output is *Vocal
  Ink Voice* (macOS: *Vocal Ink Virtual Mic*) and that the other app records
  *Vocal Ink Mic*. The mic is silent whenever Vocal Ink isn't speaking.
- **Windows made Vocal Ink Voice my speakers.** Pick your speakers or headphones
  again in *Settings → System → Sound → Output*.
- **The Windows volume sliders for Vocal Ink Voice/Mic don't change anything.**
  The driver passes the voice through at full level; use Vocal Ink's own
  volume.
- **macOS 26 (Tahoe) and later (unverified):** audio plug-ins may be listed in
  *System Settings → General → Login Items & Extensions*. If the device doesn't
  appear after a restart, check there that it is allowed.
- **Remove it by hand.** Windows: *Device Manager → Sound, video and game
  controllers → Vocal Ink Virtual Audio Device → Uninstall device* (tick
  *Attempt to remove the driver*). macOS:
  `sudo rm -rf /Library/Audio/Plug-Ins/HAL/VocalInkVirtualMic.driver && sudo killall coreaudiod`.
  Linux: delete `~/.config/pipewire/pipewire.conf.d/60-vocalink-virtual-mic.conf`
  or the *Vocal Ink virtual microphone* block in `~/.config/pulse/default.pa`,
  then log out and back in.

---

## For maintainers

### Building

- **Windows driver**: `driver/windows/VocalInkAudio.sln` with the WDK NuGet
  packages (see `driver/windows/README.md`). CI: `.github/workflows/drivers.yml`
  builds it, runs `InfVerif /w`, and uploads a test-signed package
  (`VocalInkAudio-x64`, for machines in test-signing mode) and the unsigned
  files (`VocalInkAudio-x64-unsigned`, the input for attestation signing).
- **macOS driver**: `cmake -S . -B build-driver -DVOCALINK_DRIVER_ONLY=ON
  -DVOCALINK_WITH_MAC_DRIVER=ON && cmake --build build-driver --target
  VocalInkVirtualMic` (no Qt needed). In a normal build,
  `-DVOCALINK_WITH_MAC_DRIVER=ON` also copies it into `VocalInk.app`, and
  `packaging/macos/build-dmg.sh` signs it. It is **off by default** and the
  release workflow (`build.yml`) doesn't enable it yet: turn it on there once
  the driver has been checked on real Macs (Apple Silicon and Intel, a current
  and an older macOS). Until then `drivers.yml` builds it on its own, marked
  `continue-on-error`.
- **Versions**: bump `VOCALINK_MAC_DRIVER_VERSION` in
  `driver/macos/CMakeLists.txt` when the macOS driver changes (the app offers
  the update when the bundled one is newer). For Windows, bump `<TimeStamp>`
  in `driver/windows/Source/Main/Main.vcxproj` (the INF's DriverVer) and the
  version in `VocalInkAudio.rc`. Never change the macOS `DeviceUID`
  (`VocalInkVirtualMic_UID`) or the Windows hardware ID (`ROOT\VocalInkAudio`).

### Windows driver signing

Windows 10 and 11 with Secure Boot load kernel drivers only when Microsoft has
signed them. A driver signed with our own certificate (even an EV certificate)
does not load. The route for a driver like this one is **attestation signing**
through Microsoft's Partner Center. What it takes:

1. **A registered legal entity.** The Windows Hardware Developer Program
   accepts companies/organizations, not individuals.
2. **An EV code-signing certificate** for that entity (about $300 a year from
   DigiCert, Sectigo, SSL.com and others; it lives on a hardware token or a
   cloud HSM).
3. **A Windows Hardware Developer Program account** in Partner Center,
   registered with that EV certificate (you sign a file with it during
   registration), with the legal agreements accepted.
4. **Submit the driver for each release that changes it:**
   1. Run the *Drivers* workflow and download `VocalInkAudio-x64-unsigned`.
   2. On the machine with the EV certificate, in a Developer PowerShell:
      `pwsh driver/windows/make-attestation-cab.ps1 -PackageDir <that folder> -CertThumbprint <EV cert SHA-1>`.
      It packs `VocalInkAudio.inf`, `.sys` (and `.pdb`) into
      `dist/VocalInkAudio.cab` and signs the CAB.
   3. Partner Center → *Hardware* → *Submit new hardware*: upload the CAB and
      request the Windows 10/11 x64 signatures. Attestation signing usually
      takes minutes to a few hours.
   4. Download the signed package. Put its `VocalInkAudio.inf`,
      `VocalInkAudio.sys` and `VocalInkAudio.cat` in `packaging/windows/driver/`
      and commit them, or host the zip somewhere stable and set the GitHub
      repository variables `VOCALINK_DRIVER_PACKAGE_URL` and
      `VOCALINK_DRIVER_PACKAGE_SHA256`, which `build.yml` passes to the Windows
      packaging step.
5. **Never modify the signed files** (not even the INF's text): the catalog
   covers them and any change breaks the signature. For a change, rebuild,
   bump the driver version and sign again.

`packaging/windows/deploy.ps1` then puts the three files, plus `nefconw.exe`
(nefcon v1.21.0, downloaded from its GitHub release and checked against a
pinned SHA-256), into `<app>\driver`. The Inno Setup script sees them and adds
the *Install the Vocal Ink virtual microphone* task; the portable zip carries
them too, so the app can install from there.

Attestation-signed drivers are for Windows 10/11 client editions (the INF
targets Windows 10 2004 and later), not Windows Server. They don't get the
DRM/PETrust signature attributes that need full HLK certification, which this
driver doesn't want anyway: it keeps copy-protected streams out of the
loopback.

Until a signed package exists, builds ship without the driver and the app
suggests VB-CABLE. Developers can use the test-signed CI build: `bcdedit /set
testsigning on`, reboot, import `VocalInkAudioTest.cer` into the local
machine's *Trusted Root Certification Authorities* and *Trusted Publishers*,
then install it with nefconw as above. ARM64 Windows isn't built yet; it needs
an ARM64 build and its own submission.

### macOS signing and notarization

- Every build signs the driver right after linking: ad hoc by default, or with
  `-DVOCALINK_MAC_DRIVER_SIGN_IDENTITY="Developer ID Application: …"` (then
  with the hardened runtime and a secure timestamp).
- `packaging/macos/build-dmg.sh` signs inside-out: the driver in
  `Contents/Resources` first, then the app. Set `CODESIGN_IDENTITY` to the
  Developer ID Application identity for a release; ad hoc is the default.
- For distribution outside the App Store, notarize the dmg after
  `build-dmg.sh`: `xcrun notarytool submit dist/VocalInk-*.dmg --keychain-profile
  <profile> --wait`, then `xcrun stapler staple dist/VocalInk-*.dmg`. This needs
  an Apple Developer Program membership ($99 a year) and a Developer ID
  Application certificate. Notarizing the app covers the driver inside it.
- Ad-hoc-signed HAL plug-ins load for local and test builds (as with libASPL's
  examples); this has not been verified yet on every macOS version we support.
- The install script (built by `VirtualDriverDetail::macInstallShellScript`)
  runs as root through one `osascript … with administrator privileges`: it
  copies the bundle with `ditto`, removes the quarantine attribute, sets
  `root:wheel` and 755/644 permissions, checks the signature and restarts
  `coreaudiod` with `killall` (SIGTERM; `launchctl kickstart -k` is blocked
  since macOS 14.4). It is passed inline, never from a user-writable file. The
  bundle it copies comes from the app, which may sit in a user-writable folder;
  once releases are Developer ID signed, also check the copied bundle's team ID
  before restarting `coreaudiod`.

### Linux

Nothing to sign. The config text, the `default.pa` editing and the state logic
are pure functions in `src/platform/VirtualDriverDetail.*`, covered by
`tests/test_virtualdriver.cpp` (which also installs and removes the mic for
real when a PulseAudio/PipeWire server is running).
