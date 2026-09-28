# Translations

Vocal Ink's interface ships in 12 languages: English, Español, Français,
Deutsch, Português (Brasil), Italiano, Nederlands, Polski, Türkçe, 日本語,
한국어 and 简体中文. The app picks the one that matches the computer's
language and can be switched in **Settings → Appearance → Language** (or
search "language" with Ctrl+K).

Only the interface is translated. What Vocal Ink *says* is whatever you type,
in the voice you chose.

## About these translations

The first versions were produced with the help of AI and checked
automatically (placeholders, markup, names), not yet by native speakers. The
app says so under the language picker. Corrections are very welcome: edit
`vocalink_<code>.ts` with Qt Linguist, or open an issue with the screen, the
current text and a better one.

## Files

| File | What it is |
| --- | --- |
| `vocalink_<code>.ts` | Source text and translations (Qt Linguist format). Compiled into the app at build time as `:/i18n/vocalink_<code>.qm`. |
| `vocalink_en.ts` | English holds only the plural forms ("1 voice", "2 voices"). |
| `work/` | Scratch files of `tools/i18n.py` (not committed). |

## After changing interface text

```sh
cmake --build build --target update_translations   # new and changed strings into every .ts
python3 tools/i18n.py en-plurals                    # English plural forms from "(s)"
python3 tools/i18n.py stats                         # what is left per language
python3 tools/i18n.py export de                     # untranslated strings -> i18n/work/de/*.json
# translate the JSON files ("translation" fields), then
python3 tools/i18n.py import de                     # validated back into vocalink_de.ts
python3 tools/i18n.py check                         # every language
```

`import` and `check` reject a translation whose placeholders (`%1`, `%n`),
`{variables}` or HTML tags differ from the source, and warn when a product
name or an arrow in a menu path went missing.

Write strings so they translate well: whole sentences with `%1` for the
parts that change (never glue fragments together), `qsTr("%n item(s)", "", n)`
for counts, units through `ValueSlider`'s `suffix` (it formats `50 %`/`%50`
per language), and shortcuts from `App.nativeShortcut()` rather than typed
into the text. Use `//:` comments for anything a translator can't guess.

Run the app in a language without changing your settings:

```sh
VocalInk --lang de
```
