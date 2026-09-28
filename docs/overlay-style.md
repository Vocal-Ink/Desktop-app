# Overlay style reference

Every overlay you add to OBS is a **profile**: an id, a name, a kind and a
style. The app's overlay editor, the settings file and the overlay page all
use the JSON below. Every field is optional; a missing field takes the value
from the chosen `preset`, then from the defaults for the profile's kind.

```
effective style = defaults(kind) ← preset(style.preset) ← style ← URL query options
```

Objects merge field by field (setting `font.size` keeps the preset's
`font.family`). The server always sends pages the effective, validated style
(`OverlayStyle::effective`); the page only applies URL overrides on top.

The effective style contains every field of the tables below and nothing
else: unknown keys are dropped, and the `chat` / `avatar` sections are only
present for profiles of that kind.

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
| `id` | `[a-z0-9-]`, 1–32 characters, unique. `main` always exists, comes first and is served at `/` without `?profile=`. Invalid or duplicate ids are dropped when the setting is read. |
| `name` | Free text (≤ 60 characters), shown in the app only. Empty → the id. |
| `kind` | `captions` (what you say, word by word), `chat` (Twitch messages being read aloud), `avatar` (built-in PNGtuber). Anything else → `captions`. |
| `style` | The object described below. |

The old `overlay/query` setting (URL options such as `style=ink&size=48`) is
converted into the `main` profile (`OverlayStyle::fromQuery`, see
[URL options](#url-options)) the first time the app starts with profiles, and
remembered as `overlay/legacyQuery` (`overlay/query` itself is kept, so an
older version still works). A fresh install migrates the default query
`style=subtitles`, so `overlay/legacyQuery` is `style=subtitles` there too.

Which wins on a page:
- no style options in the URL → the page follows its profile;
- URL options exactly equal to `overlay/legacyQuery` (what the app used to
  hand out, e.g. `?style=subtitles`) → the page still follows its profile, so
  the editor reaches old OBS sources. "Exactly equal" compares the style
  options as a set (order doesn't matter; `profile` and `demo` are not style
  options);
- any other options (edited by hand) → the URL wins, field by field. A
  `style=` option first applies that preset (from `config.presets`) over the
  profile's style, then the other options.

## Style

Colours are `#rrggbb` (or `#rgb`, stored as `#rrggbb`, lower case);
transparency is always a separate `…Opacity` field (0–100), because QML and
CSS read `#rrggbbaa` differently. A value that isn't valid falls back to the
preset's value, or the default when the preset doesn't set it. Numbers
outside their range are clamped (numbers given as strings, like `"48"`, are
accepted; integer fields are rounded). Unknown enum values are ignored (enum
values are matched case-insensitively). Booleans also accept `1`/`0` and
`"yes"`/`"no"`/`"on"`/`"off"`/`"true"`/`"false"`.

### `preset`
`subtitles` (default) · `ink` · `bubble` · `outline` · `karaoke` ·
`typewriter` · `neon` · `lowerthird` · `minimal`

A preset is a partial style. Each one sets the whole look (font, colours,
box, effects and the enter/word/exit animations; `bubble` and `lowerthird`
also their position), so switching presets never leaves half of the previous
one behind. Choosing one in the editor resets the fields it sets; everything
else you changed stays. The page may also style a preset's structure by name
(`.vi-preset-<name>`: the lower third's name tab, karaoke keeping spoken words
lit).

For `chat` and `avatar` profiles a preset only brings the look:
`OverlayStyle::presetForKind` leaves out `font.size`, `box.maxWidth`,
`box.align`, `position`, `name`, `history`, `timing` and `animation.word`, so
chat cards keep their size and place. The editor should reset exactly the
fields of `presetForKind(name, kind)`.

| Preset | Look |
| --- | --- |
| `subtitles` | White text on a dark translucent box, bottom centre. Words not spoken yet are faint, the newest one is wet with ink. |
| `ink` | No box: big display type (`Vocal Ink Display`) with a soft outline and shadow. Every word is there from the start, faint, and fills with ink as it is spoken, then dries to a pale tint. |
| `bubble` | White speech bubble with a tail, rounded, top left above the avatar area. Words rise in as they are spoken. |
| `outline` | Big bold uppercase text with a thick black outline, no box (gaming). Words pop in; the newest is yellow. |
| `karaoke` | All words visible at once, outlined; a sweep fills each word with the ink colour (cyan) as it is spoken, and spoken words stay lit. |
| `typewriter` | A paper card with a monospaced font (`Atkinson Hyperlegible Mono`); letters appear one by one behind a caret. |
| `neon` | Glowing text: white-hot letters with a halo in the ink colour (magenta), no box. |
| `lowerthird` | Name tab plus text box with an ink edge, sliding in from the left, bottom left. |
| `minimal` | Small plain text, a light shadow, no box and no word animation. |

### `font`
| Field | Type | Range / values | Default |
| --- | --- | --- | --- |
| `family` | string | A bundled font (`Vocal Ink Display`, `Bricolage Grotesque`, `Atkinson Hyperlegible Next`, `Atkinson Hyperlegible Mono`, `Lexend`, `OpenDyslexic`) or any font installed on the streaming PC. A comma-separated list works as fallbacks. ≤ 100 characters; quotes, `;`, braces, brackets and the like are removed. | `Bricolage Grotesque` |
| `size` | number | 12–160 (px at 1080p) | 44 |
| `weight` | number | 300–900 | 700 |
| `letterSpacing` | number | -5–30 (% of the font size) | 0 |
| `lineHeight` | number | 0.9–2.0 | 1.25 |
| `uppercase` | bool | | false |
| `italic` | bool | | false |

### `colors`
| Field | Meaning | Default |
| --- | --- | --- |
| `text` | Words (with the ink effect: once the ink has dried) | `#ffffff` |
| `ink` | Colour words fill with as they are spoken (ink, karaoke, the newest word, neon glow, indicator, the avatar's mic ring, the built-in blob) | `#b48cff` |
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
| `padding` | 0–80 px (1.3× that at the sides) | 18 |
| `radius` | 0–60 px | 14 |
| `borderWidth` | 0–12 px | 0 |
| `shadow` | 0–100 (strength) | 30 |
| `maxWidth` | 20–100 (% of the page width) | 70 |
| `align` | `left` · `center` · `right` (text inside the box) | `center` |
| `tail` | `none` · `left` · `center` · `right` (bubble tail, under the box) | `none` |

With `backgroundOpacity` 0 and no visible border the page draws no box at
all (no shadow either).

### `effects`
| Field | Range | Default |
| --- | --- | --- |
| `outline` | 0–16 px (a stroked copy of the text drawn underneath) | 0 |
| `textShadow` | 0–100 | 40 |
| `glow` | 0–100 (neon, in the ink colour) | 0 |

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

`enter`/`exit` animate the box (and each chat message). `word` is how a word
appears when it is spoken: with `ink` every word is visible from the start
(in the `unspoken` colour); with the others unspoken words keep their place
but are invisible. `type` reveals letter by letter behind a caret. With
`pop`, `bounce` and `glow` the newest word is shown in the ink colour first.

Pages honour `prefers-reduced-motion` (OBS never sets it, but browsers do):
movement becomes fades.

### `timing`
| Field | Range / values | Default |
| --- | --- | --- |
| `reveal` | `audio` (follow the real playback position) · `estimate` (words per second) · `instant` | `audio` |
| `wps` | 1–8 words per second, used by `estimate` and as the fallback before the first progress message | 2.6 |
| `hold` | 0–60 s to keep a caption after it has been spoken; -1 = until the next one (any negative value becomes -1) | 4 |

### `history`
| Field | Range | Default |
| --- | --- | --- |
| `lines` | 1–6 captions visible at once | 1 |
| `roll` | bool: older captions move up and fade instead of being replaced | false |
| `maxLines` | 1–10 text lines of one caption shown at a time; longer captions scroll to follow the word being spoken | 3 |

### `name`
| Field | Values | Default |
| --- | --- | --- |
| `show` | bool (chat messages always show the chatter's name) | false |
| `position` | `above` · `inline` · `below` | `above` |
| `text` | Replaces the voice name ("" = the voice's own name), ≤ 60 characters | `""` |

### `indicator`
| Field | Values | Default |
| --- | --- | --- |
| `show` | bool: speaking / listening / mic live indicator | false |
| `style` | `dot` · `bars` · `wave` (driven by the voice level) | `dot` |
| `position` | `before` · `after` · `corner` | `before` |

While nothing is captioned, the box shrinks to the indicator and a label
(`labels.speaking` / `listening` / `micLive`).

### `chat` (kind `chat`)
| Field | Range / values | Default |
| --- | --- | --- |
| `maxMessages` | 1–20 | 5 |
| `showBadges` | bool | true |
| `useNameColors` | bool: the chatter's Twitch colour (validated, and lightened or darkened until it is readable on the box) instead of `colors.name` | true |
| `fadeAfter` | 0–300 s, 0 = never | 30 |
| `direction` | `up` (new messages at the bottom) · `down` | `up` |
| `highlightReading` | bool: the message being read fills with ink | true |

Chat profiles start from different defaults: `font.size` 28, `box.padding`
14, `box.maxWidth` 30, `box.align` `left`, `position.anchor` `bottom-left`,
`animation.word` `none`, `name.show` true.

### `avatar` (kind `avatar`)
Images are imported in the app (Avatar page → Built-in PNGtuber) and copied
into Vocal Ink's data folder as assets named by their content hash
(`^[0-9a-f]{16}\.(png|gif|webp|jpg)$`, at most 10 MB and 4096 px, no SVG;
`OverlayStyle::importAsset`). The style refers to them by id; the page loads
them from `/assets/<id>`. An id that isn't a valid asset id becomes `""`.

| Field | Range / values | Default |
| --- | --- | --- |
| `images.idle` | asset id (mouth closed) | `""` (a built-in ink blob) |
| `images.talking` | asset id (mouth open) | `""` |
| `images.blink` | asset id (eyes closed, mouth closed) | `""` |
| `images.talkingBlink` | asset id (eyes closed, mouth open) | `""` |
| `images.micLive` | asset id shown (instead of idle) while the real mic is live | `""` |
| `threshold` | 1–50 (% mouth level that counts as talking) | 8 |
| `motion` | `none` · `bounce` · `squash` · `shake` · `float` | `bounce` |
| `motionOnlyWhileTalking` | bool | true |
| `intensity` | 0–100 | 60 |
| `blinkEvery` | 0–20 s, 0 = never (needs a blink image, or the built-in blob) | 4 |
| `flip` | bool (mirror) | false |
| `size` | 10–100 (% of the page height) | 60 |
| `shadow` | 0–100 | 0 |
| `micLiveGlow` | bool: ring in the ink colour while the real mic is live | true |
| `dimWhenIdle` | 0–100 (% darker while silent) | 0 |
| `anchor` | same values as `position.anchor` (`position.margin` and the offsets apply too) | `bottom-left` |

### `customCss`
Free CSS added after everything else, up to 8000 characters. It is sanitized
(`OverlayStyle::sanitizeCss`: comments, `@import`, `url()` except
`/fonts/<file>` and `/assets/<file>`, `image-set()`, `image()`,
`cross-fade()`, `expression()`, `behavior`, `-moz-binding`, `javascript:`,
`vbscript:`, `</` and `<!--` are removed, also when spelled with CSS escapes),
and the page itself is served with a Content-Security-Policy that only allows
its own server, so custom CSS can't load anything from elsewhere. Useful
selectors: `#vi-root` (with classes `vi-kind-<kind>` and
`vi-preset-<name>`), `.vi-box`, `.vi-line`, `.vi-word`, `.vi-word.spoken`,
`.vi-word.wet` (the newest), `.vi-name`, `.vi-ind`, `.vi-chat-message`,
`.vi-chat-name`, `.vi-badge`, `.vi-avatar`.

## Browser target

OBS's browser source is Chromium 103 (OBS 30). The page must not rely on
anything newer: no `:has()`, CSS nesting, `text-wrap`, `@scope`, container
queries, `color-mix()`, the individual `translate`/`scale`/`rotate`
properties, or `paint-order` on HTML text (outlines are a stroked copy
underneath). It honours `prefers-reduced-motion` (browsers set it; OBS
doesn't). No inline scripts (the CSP forbids them); fonts are loaded with the
`FontFace` API.

The page stays invisible until its first `config` arrives (or 1.5 s pass),
so it never flashes the default look.

## Server

`GET /` (also `/index.html`, any `?profile=`) serves the page with
`Content-Security-Policy: default-src 'self'; img-src 'self' data:;
style-src 'self' 'unsafe-inline'; font-src 'self' data:; connect-src 'self'
ws://<host>; script-src 'self'; object-src 'none'; base-uri 'none';
form-action 'none'` and `Cache-Control: no-store`. `/overlay.js`,
`/overlay.css`, `/state` (debugging), `/health`, `/fonts/<file>` (only files
listed in the font directory, `.ttf`/`.otf`/`.woff`/`.woff2`) and
`/assets/<id>` (only ids in the asset list, served with the type of the real
image format, ≤ 10 MB) complete it. Nothing ever builds a file path from the
URL.

Host names: in localhost mode only `localhost` and loopback addresses are
answered. With LAN access: IP addresses, `localhost`, this PC's own name
(also `<name>.local` and `<name>.<domain>`) and the names in
`overlay/allowedHosts` (case-insensitive). Everything else gets 403, which
blocks DNS rebinding.

## WebSocket messages (server → page)

Pages connect to `/ws?profile=<id>` (an unknown or missing id gets `main`;
when a profile is deleted its pages switch to `main`). All messages are JSON
objects with a `type`. Pages never send anything that matters: incoming
messages are capped at 1 KB (bigger ones close the connection) and ignored.
At most 32 pages are connected at once.

| `type` | Fields | To | Notes |
| --- | --- | --- | --- |
| `hello` | `v` (protocol, 2), `build` | all | First message. `build` is the app version plus a hash of the page files (`0.1.0+1a2b3c4d`). A page reloads itself when `build` changes after a reconnect. |
| `config` | `profile {id, name, kind}`, `style` (effective), `presets {name: partial style}` (`presetForKind` for this page's kind), `legacyQuery`, `fonts [{family, url, weight, style}]`, `labels {speaking, listening, micLive, demo [], badges {broadcaster, mod, vip, sub, founder}}` | all | On connect and whenever the profile, fonts, labels or legacy query change (only pages whose config actually changed get it). Missing labels fall back to English on the page. |
| `caption` | `id`, `text`, `voice`, `chatId` (only when the caption reads a chat message) | captions, chat | Replayed on connect while a caption is playing. |
| `progress` | `id`, `f` (0–1 heard), `ms`, `total`, `known` | captions, chat | ≤ ~15 per second while a caption plays. Reveal follows `f` (a time fraction; the page spreads it over the words by their length and punctuation pauses). |
| `level` | `v` 0–1, `viseme` (`""`, `A`, `I`, `U`, `E`, `O`) | avatar, `indicator.style` `wave` | ≤ 20 per second, changes under 0.02 are dropped, always ends with 0. |
| `avatar` | `talking`, `mic` | avatar | When either changes; talking has hysteresis. |
| `end` | `id` | captions, chat | The caption finished playing (hold time starts). |
| `clear` | | captions, chat | Remove everything now. |
| `speaking` | `value` bool | all | |
| `listening` | `value` bool | all | Dictation is recording. |
| `mic` | `live` bool | all | The real microphone is live. |
| `chat` | `id`, `login`, `name`, `color`, `text`, `badges []`, `action` bool, `captionId` (when reading already started), `age` (ms, replay only) | chat | Only messages that pass the chat filters. The server validates every field (`color` is `#rrggbb` or `""`, text ≤ 500 characters, badges `[a-z0-9_-]`). Text is always inserted with `textContent`. |
| `chatDelete` | `id` | chat | A moderator deleted the message. |
| `chatClearUser` | `login` | chat | Timeout or ban: remove their messages. |
| `chatClear` | | chat | Chat cleared. |

Order on connect: `hello`, `config`, `caption` (captions and chat pages,
while one plays), `speaking`, `listening`, `mic`, `avatar` (avatar pages);
chat pages then get the last 20 chat messages (oldest first, with `age`).
A page whose profile changes kind gets the new `config` followed by the same
replay.

Which chat message is being read: the server matches captions to the chat
messages it showed (most of the message's first words appear in the spoken
text, which may add "name says:", read emoji as words or cut long messages)
and links them with `chatId` / `captionId`. Messages that were skipped are
never highlighted.

## URL options

`/?profile=<id>` picks the profile. For compatibility, these options still
work and override the style (see "Which wins on a page" above). The same
mapping converts `overlay/query` into the `main` profile
(`OverlayStyle::fromQuery`). Invalid values are ignored.

| Option | Becomes |
| --- | --- |
| `style` | `preset` (`subtitles`, `ink`, `bubble`, any preset name; `plain` → `outline` at size 42, not uppercase, `effects.outline` 3, `box.maxWidth` 80) |
| `position` (`bottom`/`top`/`middle`) + `align` (`left`/`center`/`right`) | `position.anchor` (`top`+`left` → `top-left`, `middle` → `center`/`left`/`right`); `align` also sets `box.align`. `style=bubble` pins the old placement (anchor from position/align, offsets 0) |
| `tail` | `box.tail` (with `style=bubble` and no `tail`: the side of `align`, else `center`) |
| `font` | `font.family` |
| `size` | `font.size` |
| `color`, `ink`, `outlinecolor` | `colors.text`, `colors.ink`, `colors.outline` (hex with or without `#`, `rgb()`, CSS colour names) |
| `bg` | `colors.background` + `colors.backgroundOpacity` (`000000aa`, `rgba(0,0,0,0.6)`, `none`) |
| `outline` | `effects.outline` |
| `reveal` | `timing.reveal` (`word` → `audio`, `instant`) |
| `wps` | `timing.wps` |
| `hold` | `timing.hold` (more than 60 → -1) |
| `lines` | `history.maxLines` (the old option counted text lines) |
| `width` | `box.maxWidth` |
| `roll` | `history.roll`, and `history.lines` 2 (the previous caption stayed above) or 1 |
| `name` | `name.show` |
| `indicator` | `indicator.show` |
| `motion` | `motion=0` → `animation.enter`/`word`/`exit` `fade` |
| `demo` | Not a style option: the page shows sample captions, chat or avatar movement by itself (for placing the source), using the profile's look. |
