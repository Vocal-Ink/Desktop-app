import QtQuick
import Ink.Core

// A shortcut as a row of keycaps. `sequence` is portable text ("Ctrl+Alt+S").
Row {
    id: combo
    property string sequence
    property bool dim: false
    property string placeholder: ""
    // One quiet line of text instead of keycaps (dense lists).
    property bool compact: false

    spacing: 3
    Accessible.role: Accessible.StaticText
    Accessible.name: App.nativeShortcut(sequence)

    Text {
        visible: combo.compact && combo.sequence !== ""
        text: App.shortcutParts(combo.sequence).join(App.platform === "macos" ? "" : "+")
        font.family: Theme.monoFont
        font.pixelSize: Math.round(11.5 * Theme.scale)
        color: combo.dim ? Theme.accentInk : Theme.faint
    }
    Repeater {
        model: combo.sequence === "" || combo.compact ? [] : App.shortcutParts(combo.sequence)
        Keycap { label: modelData; dim: combo.dim }
    }
    Txt {
        visible: combo.sequence === "" && combo.placeholder !== ""
        text: combo.placeholder
        role: "caption"
        color: Theme.faint
    }
}
