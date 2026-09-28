# VTubing with Vocal Ink

Vocal Ink can move your avatar's mouth while its voice speaks, and make the
avatar react when it starts or stops speaking. There are two ways to do it:

- **The microphone route.** Your avatar app listens to Vocal Ink's virtual
  microphone and lip-syncs to it, exactly as it would to your own voice. This
  works with every avatar app and needs no setup in Vocal Ink.
- **A direct connection.** Vocal Ink talks to the avatar app itself: VTube
  Studio (plugin API), VSeeFace, Warudo, VNyan and VirtualMotionCapture (VMC
  protocol), veadotube mini (WebSocket), and Streamer.bot. This gives you
  mouth shapes that follow the words, hotkeys, expressions and state changes.

You can use both at once. Everything in this guide stays on your computer:
Vocal Ink only connects to apps on `127.0.0.1` unless you enter another
address yourself.

| App | Microphone route | Direct connection |
| --- | --- | --- |
| VTube Studio | Yes | Plugin API: mouth, mouth shapes, hotkeys, expressions |
| VSeeFace | Yes | VMC: mouth shapes, one expression |
| Warudo | Yes | VMC: mouth shapes, one expression |
| VNyan | Yes | VMC: mouth shapes, one expression |
| VirtualMotionCapture | Yes | VMC (port 39540) |
| veadotube mini | Yes (needed for the mouth) | WebSocket: push-to-talk, avatar states |
| veadotube (full) | Yes | No |
| PNGTuber Plus | Yes | No |
| Animaze | Yes (for the lips) | No |
| Streamer.bot | Not needed | Runs your actions |
| Vocal Ink's own PNGtuber | Not needed | Built in, shown through OBS |

All of Vocal Ink's avatar settings are on the **Avatar** page. Every
connection is off until you switch it on.

---

## How Vocal Ink moves the mouth

These settings apply to every direct connection and to the built-in
PNGtuber:

- **What moves the mouth.**
  - *Voice only* (the default): only Vocal Ink's synthesized voice.
    Soundboard sounds and your real mic don't move the mouth.
  - *Voice and sounds*: the voice and soundboard sounds. While your real mic
    is live, only the voice counts (Vocal Ink can't separate your mic from the
    sounds at that point).
  - *Everything*: the voice, sounds and your real microphone.
- **Sensitivity** (20–300 %): how wide the mouth opens for a given loudness.
  Raise it if the mouth barely opens, lower it if it's always wide open.
- **Smoothing** (0–100): how gently the mouth opens and closes. 0 follows
  every syllable; high values look calmer but lag behind.
- **Mouth shapes from the text**: when on, Vocal Ink knows which word is being
  spoken and picks the matching mouth shape (A, I, U, E or O). "moon" gets a
  round U, "see" a wide I. When off, the mouth only opens and closes.
- **Test**: moves the mouth like a short sentence on every connected app, so
  you can check the setup without speaking.

---

## Works with any app that lip-syncs from a microphone

Every avatar app can listen to a microphone. Pick Vocal Ink's virtual mic
there and the avatar lip-syncs to Vocal Ink's voice:

1. Set up the virtual microphone if you haven't yet (see
   [Vocal Ink's virtual microphone](VIRTUAL_AUDIO.md)).
2. In your avatar app's microphone or lip-sync settings, pick **Vocal Ink
   Mic** (on macOS: **Vocal Ink Virtual Mic**).
3. Say something in Vocal Ink. The mouth should move.

Some apps only read the left channel of a stereo microphone (VSeeFace is one
of them). Vocal Ink's mic carries the voice in the middle (mono), so this is
fine.

If you also use your real microphone through Vocal Ink (the live mic
passthrough), the avatar hears it too, because it goes to the same virtual mic.

---

## VTube Studio

### Plugin route (recommended)

Vocal Ink connects to VTube Studio as a plugin and drives the model's mouth
directly.

1. In VTube Studio, open the settings (gear icon) and scroll to **VTube Studio
   Plugins**. Turn on **Start API (allow plugins)** / **Allow Plugin API
   access**. Leave the port at `8001` unless you changed it.
2. In Vocal Ink, open the **Avatar** page and switch on **VTube Studio**. If
   you changed VTube Studio's port, enter the same port in Vocal Ink.
3. VTube Studio shows a popup asking whether to allow **Vocal Ink**. Click
   **Allow**.

Vocal Ink saves the access token in your system's keychain, so VTube Studio
only asks once. If you click **Deny**, Vocal Ink stops asking; use **Ask again**
on the Avatar page to get the popup again. If you revoke Vocal Ink in
VTube Studio's plugin list, Vocal Ink forgets its token and shows that access
was denied.

The status line tells you what's going on: VTube Studio isn't running, the
plugin API is switched off, the Allow popup is waiting, access was denied, or
connected (with the model name). Vocal Ink listens for the small network
announcement VTube Studio sends every two seconds, so it can tell "not running"
from "API off" and connects within a couple of seconds after you start VTube
Studio. It always tries the port from its settings first; if nothing answers
there but VTube Studio announces a different port, Vocal Ink uses that one.

**What the settings do**

| Setting | What it does |
| --- | --- |
| Mouth open parameter | The VTube Studio input parameter Vocal Ink sets to how wide the mouth is open (0–1). Default `MouthOpen`, which most models map to their mouth. |
| Mouth shape parameter | Set to how much the mouth smiles (wide vowels like I and E) or rounds (U and O). Default `MouthSmile`. Leave it empty to keep your own smile tracking while Vocal Ink speaks. |
| Tell VTube Studio the face is found | For people who don't use face tracking: VTube Studio doesn't play its "tracking lost" pose while Vocal Ink speaks. |
| Hotkey when speaking starts / stops | Runs one of the model's hotkeys (an animation, a toggle...) when the voice starts and when it finishes. |
| Hotkey when the mic goes live / is muted | The same for your real microphone passthrough. |
| Hotkey per sound | Runs a hotkey when a soundboard sound starts. |
| Expression while speaking | Turns an expression on when the voice starts and off when it ends. |
| Custom parameters | See below. |

Vocal Ink also sets VTube Studio's `VoiceA`, `VoiceI`, `VoiceU`, `VoiceE` and
`VoiceO` parameters to the current mouth shape when your version of VTube
Studio has them. Models set up for *Advanced Lip Sync* use them.

While Vocal Ink speaks, its values replace your face tracking for these
parameters. About a second after the voice stops, VTube Studio hands them back
to your tracking. Only one plugin can set a parameter at a time: if another
plugin already drives `MouthOpen`, the status details say so (error 454).

**Custom parameters.** Switch on *Custom parameters* and Vocal Ink creates two
new input parameters in VTube Studio:

- `VocalInkVolume`: how wide Vocal Ink opens the mouth (0–1).
- `VocalInkSpeaking`: 1 while the voice is talking, else 0.

Map them to anything in your model in VTube Studio's model settings (add a
parameter and choose one of them as the input), for example a blush, an arm
movement or a "talking" pose that only reacts to Vocal Ink and not to your
own voice.

### Microphone route

If you'd rather let VTube Studio do the lip sync itself:

1. In VTube Studio's settings, pick **Vocal Ink Mic** as the microphone.
2. For simple open/close lip sync, map `VoiceVolumePlusMouthOpen` (or
   `VoiceVolume`) to the model's mouth open parameter in the model settings.
3. For mouth shapes, choose **Advanced Lip Sync** and use VTube Studio's
   automatic setup, which maps `VoiceA`...`VoiceO` for you.

---

## VSeeFace

VSeeFace receives mouth shapes over the VMC protocol.

1. In VSeeFace, open **Settings → General settings** and find the
   **OSC/VMC receiver** section. Turn on the receiver and note the port
   (`39539` by default).
2. In Vocal Ink, switch on **VMC**, choose the **VSeeFace** preset (port
   `39539`) and keep the host `127.0.0.1`.
3. Set the blendshape names to match your model: **VRM 0** (A I U E O, most
   VSeeFace models) or **VRM 1** (aa ih ou ee oh).

Turning on VSeeFace's receiver switches off its own camera tracking unless you
also tick the tracking options in the receiver section (the ones that apply
VSeeFace's own face or hand tracking). Tick them if you want to keep your
face tracking and only have Vocal Ink move the mouth.

Newer VSeeFace versions have a *secondary* receiver for layering a second
source on top of the first; you can point Vocal Ink at that one while another
tracker uses the main receiver. It may be named differently in your version.

If VSeeFace's own microphone lip sync is also on and listening to Vocal Ink
Mic, both move the mouth. That's harmless, but for the cleanest result use one
of the two.

**Expression while speaking.** Enter a blendshape name (VRM 0: `Joy`, `Angry`,
`Sorrow`, `Fun`; VRM 1: `happy`, `angry`, `sad`, `relaxed`) and Vocal Ink
holds it at full strength while the voice speaks. Names are case-sensitive.

## Warudo

1. In Warudo, add a VMC receiver (the *Virtual Motion Capture* receiver asset
   or the VMC option in the tracking setup; names vary between versions) and
   set its port to `39539`.
2. In Vocal Ink, switch on **VMC** and choose the **Warudo** preset.
3. Pick **VRM 0** or **VRM 1** names to match your model's blendshapes.

## VNyan

1. In VNyan's settings, turn on the VMC receiver and note its port
   (`39539` unless you changed it).
2. In Vocal Ink, switch on **VMC** and choose the **VNyan** preset.
3. Pick **VRM 0** or **VRM 1** names to match your model.

## VirtualMotionCapture

VirtualMotionCapture is usually the app that *sends* motion to others, so it
takes extra data from assistant apps on a different port.

1. In VirtualMotionCapture's settings, turn on receiving external motion
   (OSC/VMC receive) on port `39540`.
2. In Vocal Ink, switch on **VMC** and choose the **VirtualMotionCapture**
   preset (port `39540`).

---

## veadotube mini

veadotube mini has no way to receive a mouth value: its mouth always follows a
microphone. So the mouth works through the microphone route, and Vocal Ink's
connection adds push-to-talk and avatar states.

1. In mini, pick **Vocal Ink Mic** as the microphone.
2. In mini's program settings, turn on the **WebSocket server**.
3. For push-to-talk: in mini's microphone settings, turn on push-to-talk and
   choose **use websocket** as its source.
4. In Vocal Ink, switch on **veadotube**. Vocal Ink finds a running mini by
   itself (mini announces itself in the `.veadotube/instances` folder in your
   home folder) and shows its name once connected.

**Push-to-talk.** With Vocal Ink's push-to-talk option on, Vocal Ink presses
mini's push-to-talk while the voice speaks and releases it afterwards. The
mouth then only moves for Vocal Ink's voice, not for background noise or your
real mic. If you turn the option off, mini's own settings decide.

**States.** Vocal Ink reads the list of avatar states from mini. You can pick
a state for *talking*, one for *idle*, and one for *mic live* (your real mic
passthrough is on). Leave one empty and Vocal Ink doesn't touch the state at
that moment.

## veadotube (full version)

Use the microphone route: pick **Vocal Ink Mic** as veadotube's microphone.

## PNGTuber Plus

Use the microphone route: pick **Vocal Ink Mic** as the microphone in PNGTuber
Plus.

## Animaze

Animaze does lip sync from a microphone. Pick **Vocal Ink Mic** as the audio
input for lip sync in Animaze's settings. Your camera tracking keeps working
for the rest of the face.

---

## Streamer.bot

Vocal Ink can run Streamer.bot actions when the voice starts or stops, and
when your real mic goes live or is muted. From there Streamer.bot can do
anything it controls: switch OBS scenes, trigger T.I.T.S., change lights, and
so on.

1. In Streamer.bot, open **Servers/Clients → UDP Server**, turn it on and note
   the port (`4242` by default). It needs no password.
2. Create the actions. The default names are:
   - `Vocal Ink: speaking`: runs when the voice starts. It gets the words
     being spoken as the argument `text` (use `%text%` in sub-actions).
   - `Vocal Ink: done speaking`: runs when the voice finishes.
   - Mic live and mic muted: no action by default; enter names in Vocal Ink
     if you want them.
3. In Vocal Ink, switch on **Streamer.bot** and check the port. Action names
   must match exactly, including capitals and spaces. Leave a name empty to
   skip that moment.

---

## Vocal Ink's own PNGtuber

If you don't have an avatar app, Vocal Ink has a simple PNGtuber built in:
your own pictures for mouth closed and mouth open (plus optional blinking and
"mic live" pictures), with a bounce or other motion while it talks. You set it
up on the **Avatar** page (Built-in PNGtuber), and Vocal Ink's overlay server
shows it as an OBS Browser Source, like the caption overlay. It follows the
same mouth settings as everything above. All its options are described in the
[Overlay style reference](overlay-style.md) (the `avatar` kind).

---

## Troubleshooting

| What you see | What to check |
| --- | --- |
| VTube Studio: "not running" although it's open | Turn on **Allow Plugin API access** in VTube Studio's settings. Check that the port in Vocal Ink matches VTube Studio's. |
| VTube Studio: "API is off" | Same switch: **Start API / Allow Plugin API access** in VTube Studio's settings. |
| VTube Studio: "access denied" | You clicked Deny, or Vocal Ink was removed from VTube Studio's plugin list. Use **Ask again** on the Avatar page and click **Allow**. |
| VTube Studio is connected but the mouth doesn't move | Check the mouth open parameter name (it's case-sensitive; the status details show "parameter not found" for a wrong name). In the model settings, the mouth must be mapped from that parameter. Another plugin may be driving it (error 454 in the details). |
| The mouth moves for soundboard sounds or my real mic | Set *What moves the mouth* to **Voice only**. |
| The mouth barely opens, or is always wide open | Change **Sensitivity**. |
| The mouth flickers | Raise **Smoothing**. |
| VSeeFace / Warudo / VNyan don't react | Is the app's VMC receiver on? Do the ports match? Does the model use VRM 0 names (A I U E O) or VRM 1 names (aa ih ou ee oh)? Choose the matching set. |
| VSeeFace stopped following my face | Enabling its VMC receiver switches off its own tracking. Tick the tracking options in the receiver section. |
| veadotube mini isn't found | Is mini running, with its WebSocket server on? Restart mini if it was already open when you turned the server on. |
| mini is connected but the mouth doesn't move | Pick **Vocal Ink Mic** as mini's microphone. With Vocal Ink's push-to-talk on, mini's push-to-talk must use **use websocket**; otherwise turn Vocal Ink's push-to-talk option off. |
| Streamer.bot actions don't run | Is the UDP server on and the port right? Do the action names match exactly? Is the action enabled? |
| An app on the microphone route doesn't react | Pick **Vocal Ink Mic** in that app. See [Vocal Ink's virtual microphone](VIRTUAL_AUDIO.md#troubleshooting). |
