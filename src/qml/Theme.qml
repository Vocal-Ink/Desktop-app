pragma Singleton
import QtQuick
import Ink.Core

// Design tokens. Everything visual reads from here, and everything here reads
// from the user's settings, so a change in Appearance or Accessibility
// restyles the whole app live.
QtObject {
    id: theme

    readonly property var prefs: App.prefs

    // --- Palettes --------------------------------------------------------------
    readonly property var palettes: ({
        "midnight": { bg: "#110C1F", surface: "#19132D", raised: "#221A3B", sunken: "#0C0817", line: "#332A52",
                      text: "#F1ECFF", muted: "#A69DC8", faint: "#6F6690", live: "#FF4F6D", ok: "#4FE0B0",
                      warn: "#FFC15E", sheen: "#FF7AD9", dark: true },
        "vellum":   { bg: "#F6F3FF", surface: "#EEE8FF", raised: "#FFFFFF", sunken: "#E6DFFA", line: "#DCD3F5",
                      text: "#1A1230", muted: "#5E567A", faint: "#9A92B8", live: "#D92D4B", ok: "#0F9D74",
                      warn: "#9A5B00", sheen: "#D63FB6", dark: false },
        "amethyst": { bg: "#000000", surface: "#0B0714", raised: "#150E26", sunken: "#000000", line: "#2A1F45",
                      text: "#EFE8FF", muted: "#A094C9", faint: "#655A8C", live: "#FF4F6D", ok: "#4FE0B0",
                      warn: "#FFC15E", sheen: "#FF7AD9", dark: true },
        "contrast": { bg: "#000000", surface: "#000000", raised: "#0A0A0A", sunken: "#000000", line: "#FFFFFF",
                      text: "#FFFFFF", muted: "#FFFFFF", faint: "#C8C8C8", live: "#FF6B81", ok: "#5CFFC1",
                      warn: "#FFE14D", sheen: "#FFE14D", dark: true, accent: "#C9A7FF", focus: "#FFE14D" }
    })

    readonly property SystemPalette systemPalette: SystemPalette { colorGroup: SystemPalette.Active }
    readonly property bool systemDark: {
        const hints = Qt.styleHints
        if (hints && hints.colorScheme !== undefined && hints.colorScheme !== 0)
            return hints.colorScheme === 2
        const c = systemPalette.window
        return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) < 0.5
    }
    readonly property string themeId: {
        const t = prefs["ui/theme"] || "midnight"
        if (t === "system")
            return systemDark ? "midnight" : "vellum"
        return palettes[t] ? t : "midnight"
    }
    readonly property var pal: palettes[themeId]
    readonly property bool dark: pal.dark
    readonly property bool highContrast: themeId === "contrast"

    readonly property color bg: pal.bg
    readonly property color surface: pal.surface
    readonly property color raised: pal.raised
    readonly property color sunken: pal.sunken
    readonly property color line: pal.line
    readonly property color text: pal.text
    readonly property color muted: pal.muted
    readonly property color faint: pal.faint
    readonly property color live: pal.live
    readonly property color ok: pal.ok
    readonly property color warn: pal.warn
    readonly property color sheen: pal.sheen

    // The signature violet unless the user picked another ink.
    readonly property color accent: pal.accent ? pal.accent : (prefs["ui/accent"] || "#8c52ff")
    // Accent used for text and thin strokes: nudged for contrast on this paper.
    readonly property color accentText: highContrast ? accent : dark ? Qt.lighter(accent, 1.28) : Qt.darker(accent, 1.25)
    readonly property color accentHover: dark ? Qt.lighter(accent, 1.12) : Qt.darker(accent, 1.08)
    readonly property color accentPressed: dark ? Qt.darker(accent, 1.15) : Qt.darker(accent, 1.22)
    readonly property color accentInk: luminance(accent) > 0.45 ? "#140E24" : "#FFFFFF"
    // Tinted washes for selected rows, chips, the listening glow.
    readonly property color accentWash: alpha(accent, dark ? 0.16 : 0.12)
    readonly property color accentWashStrong: alpha(accent, dark ? 0.28 : 0.2)
    readonly property color liveWash: alpha(live, dark ? 0.18 : 0.12)
    readonly property color okWash: alpha(ok, dark ? 0.16 : 0.12)
    readonly property color warnWash: alpha(warn, dark ? 0.16 : 0.14)
    readonly property color hover: alpha(text, dark ? 0.06 : 0.05)
    readonly property color pressed: alpha(text, dark ? 0.1 : 0.09)
    readonly property color scrim: alpha("#07040E", dark ? 0.66 : 0.4)

    // Words: ink that is still wet (being spoken), dry (spoken), not yet written.
    readonly property color inkDry: text
    readonly property color inkWet: highContrast ? pal.focus : accentText
    readonly property color inkUnwritten: highContrast ? "#9A9A9A" : alpha(text, dark ? 0.32 : 0.36)

    readonly property color focus: highContrast ? pal.focus : boldFocus ? (dark ? "#FFE14D" : "#1A1230") : accentText

    // --- Type -----------------------------------------------------------------
    readonly property real scale: Math.max(0.8, Math.min(2.5, (prefs["ui/fontScale"] || 100) / 100))
    readonly property string fontChoice: prefs["ui/font"] || "atkinson"
    readonly property string uiFont: fontChoice === "opendyslexic" ? "OpenDyslexic"
                                     : fontChoice === "lexend" ? "Lexend"
                                     : fontChoice === "system" ? Qt.application.font.family
                                     : "Atkinson Hyperlegible Next"
    readonly property string displayFont: fontChoice === "atkinson" ? "Vocal Ink Display" : uiFont
    readonly property string stageFont: fontChoice === "atkinson" ? "Bricolage Grotesque" : uiFont
    readonly property string monoFont: "Atkinson Hyperlegible Mono"

    readonly property real fsXs: Math.round(12 * scale)
    readonly property real fsSm: Math.round(13.5 * scale)
    readonly property real fsMd: Math.round(15 * scale)
    readonly property real fsLg: Math.round(17 * scale)
    readonly property real fsXl: Math.round(21 * scale)
    readonly property real fsXxl: Math.round(30 * scale)
    readonly property real fsHero: Math.round(46 * scale)
    readonly property real stageSize: Math.round(40 * scale * (prefs["ui/stageScale"] || 100) / 100)
    readonly property real composerSize: Math.round((prefs["ui/composerSize"] || 17) * scale)

    readonly property real letterSpacingPct: (prefs["a11y/letterSpacing"] || 0) / 100
    readonly property real lineHeight: Math.max(1.0, (prefs["a11y/lineSpacing"] || 100) / 100)
    function tracking(size) { return size * letterSpacingPct }

    // --- Space & shape ------------------------------------------------------------
    readonly property string density: prefs["ui/density"] || "comfortable"
    readonly property real unit: density === "compact" ? 6 : density === "spacious" ? 10 : 8
    readonly property real s1: unit / 2
    readonly property real s2: unit
    readonly property real s3: unit * 1.5
    readonly property real s4: unit * 2
    readonly property real s5: unit * 3
    readonly property real s6: unit * 4
    readonly property real s8: unit * 6

    readonly property bool largeTargets: prefs["a11y/largeTargets"] === true
    readonly property real control: Math.round(Math.max(largeTargets ? 52 : density === "compact" ? 34 : density === "spacious" ? 46 : 40,
                                                        fsMd * 2.4))
    readonly property real controlSm: Math.round(control * 0.8)

    readonly property string corners: prefs["ui/corners"] || "soft"
    readonly property real radius: corners === "sharp" ? 3 : corners === "round" ? 20 : 10
    readonly property real radiusSm: corners === "sharp" ? 2 : corners === "round" ? 14 : 7
    readonly property real radiusLg: corners === "sharp" ? 4 : corners === "round" ? 28 : 16

    readonly property bool boldFocus: prefs["a11y/focusRing"] === "bold" || highContrast
    readonly property real focusWidth: boldFocus ? 3.5 : 2
    readonly property real hairline: highContrast ? 2 : 1

    // --- Motion ---------------------------------------------------------------
    readonly property string motion: prefs["ui/motion"] || "full"
    readonly property bool motionOn: motion !== "off"
    readonly property bool reducedMotion: motion !== "full"
    readonly property real motionFactor: motion === "off" ? 0 : motion === "reduced" ? 0.55 : 1
    function dur(ms) { return Math.round(ms * motionFactor) }
    readonly property int fast: dur(120)
    readonly property int normal: dur(220)
    readonly property int slow: dur(380)
    // Movement distance: reduced motion keeps fades but drops travel.
    function travel(px) { return reducedMotion ? 0 : px }

    // --- Helpers --------------------------------------------------------------
    function alpha(c, a) {
        const col = Qt.color(c)
        return Qt.rgba(col.r, col.g, col.b, a)
    }
    function mix(a, b, t) {
        const x = Qt.color(a), y = Qt.color(b)
        return Qt.rgba(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t)
    }
    function luminance(c) {
        if (!c || c === "")
            return 0
        const col = Qt.color(c)
        function ch(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * ch(col.r) + 0.7152 * ch(col.g) + 0.0722 * ch(col.b)
    }

    // Inks the user can pick. Violet first: it's ours.
    readonly property var accents: [
        { color: "#8c52ff", name: qsTr("Vocal violet") },
        { color: "#B18CFF", name: qsTr("Lilac") },
        { color: "#6A33F0", name: qsTr("Deep violet") },
        { color: "#D16BFF", name: qsTr("Orchid") },
        { color: "#FF6FB5", name: qsTr("Rose") },
        { color: "#FF8A5B", name: qsTr("Ember") },
        { color: "#FFC857", name: qsTr("Marigold") },
        { color: "#3FD6A6", name: qsTr("Mint") },
        { color: "#48B8FF", name: qsTr("Sky") }
    ]
}
