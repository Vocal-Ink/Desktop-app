# Using Vocal Ink with OBS Studio

Vocal Ink can put what you say on your stream, so viewers can read along while
the voice speaks. There are four ways to do it. Use one, or combine them:

| Feature | What viewers see | Needs the OBS connection? |
| --- | --- | --- |
| [Caption overlay](#1-caption-overlay-browser-source) | Animated captions or a speech bubble, drawn by a Browser Source | No (only for the one-click setup) |
| [Text-source subtitles](#2-subtitles-in-a-text-source) | Your words in an ordinary OBS Text source that you style yourself | Yes |
| [Closed captions](#3-closed-captions-for-twitch-and-youtube) | Native CC that viewers switch on in the Twitch/YouTube player | Yes |
| [Speaking indicator](#4-speaking-indicator-for-pngtuber-avatars) | A source (e.g. a "talking" avatar) shown only while the voice speaks | Yes |

Most streamers start with the **caption overlay**: it looks good out of the box
and needs no OBS setup beyond adding one source.

---

## Connecting Vocal Ink to OBS

The subtitle, closed-caption and indicator features, and the one-click overlay
setup, talk to OBS through its built-in WebSocket server. OBS Studio 28 and
newer include it. On older versions, update OBS.

### Turn on OBS's WebSocket server

1. In OBS, open **Tools → WebSocket Server Settings**.
2. Tick **Enable WebSocket server**.
3. Leave **Server Port** at `4455` unless another program already uses it.
4. Keep **Enable Authentication** ticked and click **Show Connect Info** to see
   (and copy) the **Server Password**.
5. Click **Apply**.

### Connect from Vocal Ink

1. In Vocal Ink, open **Settings → OBS** and turn on the OBS connection.
2. Host: `127.0.0.1` if OBS runs on this computer. If OBS runs on another
   PC, enter that PC's IP address (see [Troubleshooting](#troubleshooting)).
3. Port: `4455`, or whatever you set in OBS.
4. Password: paste the server password. Vocal Ink keeps it in your system's
   keychain, not in a plain settings file.

The status line should read **Connected to OBS 31.0.2** (with your OBS
version). If OBS isn't running yet, that's fine: Vocal Ink keeps retrying in the
background and connects once OBS starts.

---

## 1. Caption overlay (Browser Source)

Vocal Ink runs a small web page on your computer that shows your captions with
a word-by-word reveal. OBS displays it with a **Browser Source**. The page is
transparent and shows nothing while you're silent.

### One-click setup

With the OBS connection set up, click **Add caption overlay to OBS** in
**Settings → OBS**. Vocal Ink adds a 1920×1080 Browser Source called
*Vocal Ink captions* to the scene that's currently live. Click it again after
you change the style: Vocal Ink updates the existing source instead of adding a
second one.

### Manual setup

1. Copy the overlay URL from **Settings → OBS**. By default it's
   `http://127.0.0.1:7342/?style=subtitles`.
2. In OBS, click **+** under *Sources* and choose **Browser**.
3. Paste the URL, set **Width** `1920` and **Height** `1080` (or your canvas
   size), and leave **Local file** unticked.
4. Optionally clear the **Custom CSS** box (the page is transparent anyway).
5. Click **OK**. Stretch the source to fill the canvas and place it above your
   game or camera.

To see where captions will appear while you arrange the scene, add `&demo=1`
to the URL. Sample captions then loop. Remove it when you're done.

### Styles

- **`style=subtitles`** (default): white text on a translucent dark box,
  centred near the bottom, like TV subtitles.
- **`style=bubble`**: a white speech bubble with a tail. Place it next to your
  PNGtuber or avatar with `align=left` or `align=right`.
- **`style=plain`**: outlined text with no box, for custom scenes and frames.

### URL parameters

Add parameters to the overlay URL after `?`, separated by `&`. For example:
`http://127.0.0.1:7342/?style=bubble&align=left&name=1`.

| Parameter | Values | Default | What it does |
| --- | --- | --- | --- |
| `style` | `subtitles`, `bubble`, `plain` | `subtitles` | Overall look (see above). |
| `position` | `bottom`, `top`, `middle` | `bottom` | Vertical placement. |
| `align` | `center`, `left`, `right` | `center` | Horizontal placement and text alignment. |
| `tail` | `left`, `center`, `right`, `none` | same as `align` | Where the bubble's tail points (bubble style). |
| `font` | a font installed on the OBS computer, e.g. `Comic Sans MS` | system UI font | Font family. A comma-separated list works too. |
| `size` | pixels | `42` | Text size. |
| `color` | CSS colour, or hex without `#` (`ffcc00`) | white; dark ink for `bubble` | Text colour. |
| `bg` | CSS colour including alpha, e.g. `rgba(0,0,0,0.6)` or `000000aa`; `none` | translucent dark; white for `bubble`; none for `plain` | Box or bubble background. |
| `outline` | pixels | `0`; `3` for `plain` | Outline around the letters. |
| `outlinecolor` | colour | `000000` | Colour of the outline. |
| `reveal` | `word`, `instant` | `word` | Reveal words one by one, or show the whole message at once. |
| `wps` | words per second | `3.5` | Reveal speed. When the voice finishes, any remaining words appear at once. |
| `hold` | seconds; `-1` = until the next message | `4` | How long a caption stays after the voice finishes. |
| `lines` | number | `2` | Maximum visible lines. Older lines scroll away. |
| `width` | percent of the source width | `70`; `42` for `bubble`; `80` for `plain` | Maximum caption width. |
| `roll` | `1`, `0` | `1`; `0` for `bubble` | `1` keeps the previous message (dimmed) above a new one; `0` replaces it. |
| `name` | `1`, `0` | `0` | Show the name of the voice above the text. |
| `indicator` | `1`, `0` | `0` | Pulsing "speaking…" dot while the voice plays, and a mic "listening…" badge while Vocal Ink listens to you. |
| `motion` | `1`, `0` | `1` | `0` turns off movement (fades only). The page also respects the system's *reduce motion* setting. |
| `demo` | `1` | off | Loop sample captions, for positioning the source. |

**Colours:** `#` has a special meaning in URLs, so write hex colours without it
(`color=ffe066`) or as `%23ffe066`.

**Examples**

- Default subtitles: `http://127.0.0.1:7342/`
- Speech bubble next to an avatar at the bottom left, with the voice name:
  `http://127.0.0.1:7342/?style=bubble&align=left&name=1`
- Big outlined text at the top: `http://127.0.0.1:7342/?style=plain&position=top&size=60&outline=4`
- Darker box, three lines, captions stay 8 seconds: `http://127.0.0.1:7342/?bg=rgba(0,0,0,0.85)&lines=3&hold=8`
- Yellow text in your own font, whole message at once:
  `http://127.0.0.1:7342/?font=Comic%20Sans%20MS&color=ffe066&reveal=instant`
- Captions plus a speaking/listening badge: `http://127.0.0.1:7342/?indicator=1`

You can also set the query part (everything after `?`) in **Settings → OBS**.
The copied URL and the one-click button then use it.

---

## 2. Subtitles in a Text source

Vocal Ink types what you say into an OBS **Text (GDI+)** (Windows) or
**Text (FreeType 2)** (macOS/Linux) source. You style that source in OBS with
its font, colour, outline and background.

1. In **Settings → OBS**, turn on subtitles.
2. Pick an existing text source from the list, or click **Create text source**
   to add one (48 pt Arial) to the scene that's currently live.
3. Choose after how many seconds the text is cleared. `0` keeps the last
   sentence on screen.
4. Click **Test subtitle**. "Vocal Ink test subtitle" should appear in OBS and
   disappear after the delay.

---

## 3. Closed captions for Twitch and YouTube

Vocal Ink can send what you say as real closed captions (CEA-608) inside your
stream. Viewers turn them on with the **CC** button in the player.

1. In **Settings → OBS**, turn on closed captions.
2. Go live from OBS as usual.

Good to know:

- Captions are only sent **while you're streaming**. They aren't part of local
  recordings.
- **Twitch** shows embedded captions through the player's CC button.
- **YouTube** needs *Closed captions → Embedded 608/708* selected in the
  stream settings in YouTube Studio.
- CEA-608 is an old broadcast standard. Plain Latin letters work best, and
  emoji and some accented characters may be dropped.

---

## 4. Speaking indicator for PNGtuber avatars

Vocal Ink can show an OBS source while the voice speaks and hide it again when
it stops. PNGtubers use this to make their avatar "talk":

1. Add two image sources to your scene: *Avatar idle* (always visible) and
   *Avatar talking* above it (for example, with the mouth open).
2. In **Settings → OBS**, turn on the speaking indicator and choose
   *Avatar talking*.
3. Hide *Avatar talking* in OBS (click the eye). Vocal Ink shows it only while
   speaking.

The source must be directly in the scene that's live, not inside a group or
a nested scene. Any source type works, such as an image, a GIF or a media
source.

If you only need a small on-screen hint, use the overlay's `indicator=1`
parameter instead.

---

## Troubleshooting

**"OBS is not running or the WebSocket server is off (Tools → WebSocket Server Settings)"**
Start OBS, and check that **Enable WebSocket server** is ticked and that the
port matches. Vocal Ink retries on its own, so you don't need to restart it.

**"Wrong OBS WebSocket password"**
Copy the password again from **Tools → WebSocket Server Settings → Show Connect
Info**. Passwords are case-sensitive. If you clicked *Generate Password* in OBS,
the old one no longer works. Vocal Ink stops retrying after a wrong password,
so save the new one (or press *Reconnect*) to try again.

**"OBS did not answer…"**
You're probably on OBS 27 or older with the old obs-websocket 4.x plugin.
Update to OBS 28 or newer.

**OBS runs on a second (streaming) PC**
- For the connection, enter the streaming PC's IP address as the host, and
  allow port 4455 through its firewall.
- For the overlay, turn on **Allow LAN access** for the overlay in Vocal Ink.
  In the Browser Source on the streaming PC, replace `127.0.0.1` in the URL
  with the IP address of the PC running Vocal Ink, e.g.
  `http://192.168.1.20:7342/`. Allow port 7342 through that PC's firewall. The
  one-click button always uses `127.0.0.1`, so edit the URL afterwards.

**The overlay shows nothing**
- Make sure Vocal Ink is running and the caption overlay is turned on.
- Open the overlay URL in a normal web browser and add `&demo=1`. If you see
  sample captions there, the page works. `http://127.0.0.1:7342/health`
  should say `ok`.
- In the Browser Source properties, check the URL and size, then click
  **Refresh cache of current page**.
- The page reconnects automatically after Vocal Ink restarts. You don't need to
  touch OBS.

**"The OBS caption overlay could not start on port 7342"**
Another program uses that port. Pick a different overlay port in
**Settings → OBS** and update the Browser Source URL (or click the one-click
button again).

**Captions look blurry or too small**
Give the Browser Source the same width and height as your canvas (usually
1920×1080) and don't scale it. Change the text size with `size=` instead.

**The text source or indicator doesn't change**
The status line in **Settings → OBS** says what went wrong. Usually a source
was renamed in OBS, or the indicator source isn't in the scene that's live.
Pick the source again from the list.

**Closed captions don't show up**
They only appear while you're live, and viewers must turn on CC. On YouTube,
check the *Embedded 608/708* setting. Some platforms and outputs (for example
WHIP) don't carry embedded captions.

### Privacy

The overlay server only accepts connections from this computer unless you turn
on LAN access, and it refuses pages from other websites. The OBS password
itself is never sent over the network: Vocal Ink answers OBS's login challenge
with a hash of it.
