# Overlay style reference

Every overlay you add to OBS is a **profile**: an id, a name, a kind and a
style. The app's overlay editor, the settings file and the overlay page all
use the JSON below. Every field is optional; a missing field takes the value
from the chosen `preset`, then from the defaults for the profile's kind.

```
effective style = defaults(kind) ← preset(style.preset) ← style ← URL query options
```

Objects merge field by field (setting `font.size` keeps the preset's
`font.family`). The server always sends pages the effective, validated style;
the page only applies URL overrides on top.

## Profiles

Stored in the `overlay/profiles` setting as a JSON array:

```json
[
  { "id": "main", "name": "Captions", "kind": "captions", "style": { "preset": "ink" } },
  { "id": "chat", "name": "Chat", "kind": "chat", "style": { "chat": { "maxMessages": 6 } } },
  { "id": "me", "name": "PNGtuber", "kind": "avatar", "style": { "avatar": { "motion": "squash" } } }
]
```

| Field | Rules |
| --- | --- |
| `id` | `[a-z0-9-]`, 1–32 characters, unique. `main` always exists and is served at `/` without `?profile=`. |
| `name` | Free text, shown in the app only. |
| `kind` | `captions` (what you say, word by word), `chat` (Twitch messages being read aloud), `avatar` (built-in PNGtuber). |
| `style` | The object described below. |

The old `overlay/query` setting (URL options such as `style=ink&size=48`) is
converted into the `main` profile the first time the app starts with
profiles, and remembered as `overlay/legacyQuery` (`overlay/query` itself is
kept, so an older version still works).

Which wins on a page:
- no style options in the URL → the page follows its profile;
- URL options exactly equal to `overlay/legacyQuery` (what the app used to
  hand out, e.g. `?style=subtitles`) → the page still follows its profile, so
  the editor reaches old OBS sources;
- any other options (edited by hand) → the URL wins, field by field.

## Style

Colours are `#rrggbb` (or `#rgb`); transparency is always a separate
`…Opacity` field (0–100), because QML and CSS read `#rrggbbaa` differently.
Anything else is replaced by the default. Numbers outside their range are clamped. Unknown enum values are
ignored (the default is used).

### `preset`
`subtitles` (default) · `ink` · `bubble` · `outline` · `karaoke` ·
`typewriter` · `neon` · `lowerthird` · `minimal`

A preset is a partial style. Choosing one in the editor resets the fields it
sets; everything else you changed stays.

| Preset | Look |
| --- | --- |
| `subtitles` | White text on a dark translucent box, bottom centre. |
| `ink` | Words fill with ink colour as they are spoken, no box. |
| `bubble` | Speech bubble with a tail, rounded, above the avatar area. |
| `outline` | Big bold text with a thick outline, no box (gaming). |
| `karaoke` | All words visible at once, the spoken ones light up. |
| `typewriter` | Letters appear one by one with a caret. |
| `neon` | Glowing text in the ink colour. |
| `lowerthird` | Name bar plus text box sliding in from the left, bottom left. |
| `minimal` | Small plain text, no effects. |

### `font`
| Field | Type | Range / values | Default |
| --- | --- | --- | --- |
| `family` | string | A bundled font (`Vocal Ink Display`, `Bricolage Grotesque`, `Atkinson Hyperlegible Next`, `Lexend`, `OpenDyslexic`) or any font installed on the streaming PC | `Bricolage Grotesque` |
| `size` | number | 12–160 (px at 1080p) | 44 |
| `weight` | number | 300–900 | 700 |
| `letterSpacing` | number | -5–30 (% of the font size) | 0 |
| `lineHeight` | number | 0.9–2.0 | 1.25 |
| `uppercase` | bool | | false |
| `italic` | bool | | false |

### `colors`
| Field | Meaning | Default |
| --- | --- | --- |
| `text` | Words (spoken words when there is no ink effect) | `#ffffff` |
| `ink` | Colour words fill with as they are spoken (ink, karaoke, neon glow, indicator) | `#b48cff` |
| `unspoken` | Words not spoken yet (ink, karaoke) | `#ffffff` |
| `unspokenOpacity` | 0–100 | 35 |
| `background` | Box colour | `#120d1f` |
| `backgroundOpacity` | Box opacity, 0–100 | 72 |
| `border` | Box border | `#ffffff` |
| `borderOpacity` | 0–100 | 12 |
| `outline` | Text outline | `#000000` |
| `shadow` | Text and box shadow | `#000000` |
| `name` | Voice / chatter name | `#d9c6ff` |

### `box`
| Field | Range / values | Default |
| --- | --- | --- |
| `padding` | 0–80 px | 18 |
| `radius` | 0–60 px | 14 |
| `borderWidth` | 0–12 px | 0 |
| `shadow` | 0–100 (strength) | 30 |
| `maxWidth` | 20–100 (% of the page width) | 70 |
| `align` | `left` · `center` · `right` (text inside the box) | `center` |
| `tail` | `none` · `left` · `center` · `right` (bubble tail) | `none` |

### `effects`
| Field | Range | Default |
| --- | --- | --- |
| `outline` | 0–16 px (a stroked copy of the text drawn underneath) | 0 |
| `textShadow` | 0–100 | 40 |
| `glow` | 0–100 (neon) | 0 |

### `position`
| Field | Range / values | Default |
| --- | --- | --- |
| `anchor` | `top-left` · `top` · `top-right` · `left` · `center` · `right` · `bottom-left` · `bottom` · `bottom-right` | `bottom` |
| `offsetX` | -50–50 (% of the page width) | 0 |
| `offsetY` | -50–50 (% of the page height) | 0 |
| `margin` | 0–200 px from the page edge | 48 |

### `animation`
| Field | Values | Default |
| --- | --- | --- |
| `enter` | `none` · `fade` · `rise` · `pop` · `slide` | `rise` |
| `word` | `none` · `fade` · `rise` · `pop` · `ink` · `glow` · `bounce` · `type` | `ink` |
| `exit` | `none` · `fade` · `sink` · `shrink` | `fade` |
| `speed` | 25–300 (% of normal speed) | 100 |

Pages honour `prefers-reduced-motion` (OBS never sets it, but browsers do).

### `timing`
| Field | Range / values | Default |
| --- | --- | --- |
| `reveal` | `audio` (follow the real playback position) · `estimate` (words per second) · `instant` | `audio` |
| `wps` | 1–8 words per second, used by `estimate` and as the fallback before the first progress message | 2.6 |
| `hold` | 0–60 s to keep a caption after it has been spoken; -1 = until the next one | 4 |

### `history`
| Field | Range | Default |
| --- | --- | --- |
| `lines` | 1–6 captions visible at once | 1 |
| `roll` | bool: older captions move up and fade instead of being replaced | false |

### `name`
| Field | Values | Default |
| --- | --- | --- |
| `show` | bool | false |
| `position` | `above` · `inline` · `below` | `above` |
| `text` | Replaces the voice name ("" = the voice's own name) | `""` |

### `indicator`
| Field | Values | Default |
| --- | --- | --- |
| `show` | bool: speaking / listening / mic live indicator | false |
| `style` | `dot` · `bars` · `wave` (driven by the voice level) | `dot` |
| `position` | `before` · `after` · `corner` | `before` |

### `chat` (kind `chat`)
| Field | Range / values | Default |
| --- | --- | --- |
| `maxMessages` | 1–20 | 5 |
| `showBadges` | bool | true |
| `useNameColors` | bool: the chatter's Twitch colour (validated) instead of `colors.name` | true |
| `fadeAfter` | 0–300 s, 0 = never | 30 |
| `direction` | `up` (new messages at the bottom) · `down` | `up` |
| `highlightReading` | bool: the message being read fills with ink | true |

### `avatar` (kind `avatar`)
Images are imported in the app (Avatar page → Built-in PNGtuber) and copied
into Vocal Ink's data folder as assets named by their content hash
(`^[0-9a-f]{16}\.(png|gif|webp|jpg)$`, at most 10 MB and 4096 px, no SVG).
The style refers to them by id; the page loads them from `/assets/<id>`.

| Field | Range / values | Default |
| --- | --- | --- |
| `images.idle` | asset id (mouth closed) | `""` (a built-in ink blob) |
| `images.talking` | asset id (mouth open) | `""` |
| `images.blink` | asset id (eyes closed, mouth closed) | `""` |
| `images.talkingBlink` | asset id (eyes closed, mouth open) | `""` |
| `images.micLive` | asset id shown while the real mic is live | `""` |
| `threshold` | 1–50 (% mouth level that counts as talking) | 8 |
| `motion` | `none` · `bounce` · `squash` · `shake` · `float` | `bounce` |
| `motionOnlyWhileTalking` | bool | true |
| `intensity` | 0–100 | 60 |
| `blinkEvery` | 0–20 s, 0 = never (needs a blink image) | 4 |
| `flip` | bool (mirror) | false |
| `size` | 10–100 (% of the page height) | 60 |
| `shadow` | 0–100 | 0 |
| `micLiveGlow` | bool: ring in the ink colour while the real mic is live | true |
| `dimWhenIdle` | 0–100 (% darker while silent) | 0 |
| `anchor` | same values as `position.anchor` | `bottom-left` |

### `customCss`
Free CSS added after everything else, up to 8000 characters. It is sanitized
(`@import`, `url()` except `/fonts/…` and `/assets/…`, `expression()`,
`behavior`, `-moz-binding`, `javascript:` and `</` are removed), and the page
itself is served with a Content-Security-Policy that only allows its own
server, so custom CSS can't load anything from elsewhere. Useful selectors: `#vi-root`, `.vi-line`, `.vi-word`,
`.vi-word.spoken`, `.vi-name`, `.vi-indicator`, `.vi-chat-message`,
`.vi-avatar`.

## Browser target

OBS's browser source is Chromium 103 (OBS 30). The page must not rely on
anything newer: no `:has()`, CSS nesting, `text-wrap`, `@scope`, or
`paint-order` on HTML text (outlines are a stroked copy underneath). It
honours `prefers-reduced-motion` (browsers set it; OBS doesn't).

The page stays invisible until its first `config` arrives (or 1.5 s pass),
so it never flashes the default look.

## WebSocket messages (server → page)

All messages are JSON objects with a `type`. Pages never send anything that
matters (incoming messages are capped at 1 KB and ignored).

| `type` | Fields | Notes |
| --- | --- | --- |
| `hello` | `v` (protocol, 2), `build` | First message. A page reloads itself when `build` changes. |
| `config` | `profile {id, name, kind}`, `style` (effective), `presets {name: partial style}`, `legacyQuery`, `fonts [{family, url}]`, `labels {speaking, listening, micLive, demo []}` | On connect and whenever the profile, fonts or language change. |
| `caption` | `id`, `text`, `voice` | Replayed on connect while a caption is on screen. |
| `progress` | `id`, `f` (0–1 heard), `ms`, `total`, `known` | ~15 Hz while a caption plays. Reveal follows `f`. |
| `level` | `v` 0–1, `viseme` (`""`, `A`, `I`, `U`, `E`, `O`) | Avatar pages and `wave` indicators only; ≤20 Hz, ends with 0. |
| `avatar` | `talking`, `mic` | Avatar pages; talking has hysteresis. |
| `end` | `id` | The caption finished playing (hold time starts). |
| `clear` | | Remove everything now. |
| `speaking` | `value` bool | |
| `listening` | `value` bool | Dictation is recording. |
| `mic` | `live` bool | The real microphone is live. |
| `chat` | `id`, `name`, `color`, `text`, `badges []`, `action` bool | Chat pages only; only messages that pass the chat filters. Text is always inserted with `textContent`. |
| `chatDelete` | `id` | A moderator deleted the message. |
| `chatClearUser` | `login` | Timeout or ban: remove their messages. |
| `chatClear` | | Chat cleared. |

Order on connect: `hello`, `config`, `caption`, `speaking`, `listening`,
`mic`, `avatar`; chat pages also get the last 20 messages.

## URL options

`/?profile=<id>` picks the profile. For compatibility, these options still
work and override the style (see "Which wins on a page" above): `style`
(= preset), `position`, `align`, `tail`, `font`, `size`, `color`, `bg`,
`ink`, `outline`, `outlinecolor`, `reveal`, `wps`, `hold`, `lines`, `width`,
`roll`, `name`, `indicator`, `motion`, `demo`.
