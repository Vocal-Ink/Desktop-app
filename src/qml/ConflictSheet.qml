import QtQuick
import QtQuick.Layouts
import Ink.Core

// "That shortcut is already used by …" with the choice to move it here.
Sheet {
    id: sheet
    property string action: ""
    property string seq: ""
    property var names: []

    title: qsTr("%1 is already in use").arg(App.nativeShortcut(seq))
    message: qsTr("It's assigned to: %1. Use it here instead? The other one will be left without a shortcut.").arg(names.join(", "))
    iconName: "keyboard"
    footer: [
        PillButton {
            text: qsTr("Use it here")
            kind: "primary"
            onClicked: { App.keybinds.bind(sheet.action, sheet.seq, true); sheet.close() }
        },
        PillButton { text: qsTr("Keep the old one"); kind: "ghost"; onClicked: sheet.close() }
    ]
}
