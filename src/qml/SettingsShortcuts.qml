import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Every keybindable action, grouped, with conflict and safety checks.
ScrollPage {
    id: page
    title: qsTr("Shortcuts")
    subtitle: qsTr("Global shortcuts work while other apps and games have focus. Click a shortcut and press the new keys; Backspace clears it.")
    property string filter: ""

    actions: [
        PillButton { kind: "ghost"; iconName: "rotate-ccw"; text: qsTr("Reset all"); onClicked: resetAll.open() }
    ]

    Rectangle {
        visible: !App.hotkeysSupported
        Layout.fillWidth: true
        implicitHeight: warn.implicitHeight + Theme.s3 * 2
        radius: Theme.radius
        color: Theme.warnWash
        RowLayout {
            id: warn
            anchors.fill: parent
            anchors.margins: Theme.s3
            spacing: Theme.s3
            Icon { name: "keyboard-off"; color: Theme.warn; Layout.alignment: Qt.AlignTop }
            Txt { Layout.fillWidth: true; font.pixelSize: Theme.fsSm; text: App.hotkeysUnsupportedReason }
        }
    }

    Field {
        Layout.fillWidth: true
        iconName: "search"
        label: qsTr("Find a shortcut")
        placeholderText: qsTr("Find an action, e.g. “mute” or “voice”")
        onTextChanged: page.filter = text.toLowerCase()
    }

    // One card per category, built from the model.
    Repeater {
        model: App.keybinds.categories
        Card {
            id: group
            required property string modelData
            Layout.fillWidth: true
            title: modelData
            visible: rows.visibleCount > 0
            pad: Theme.s4

            ColumnLayout {
                id: rows
                property int visibleCount: 0
                Layout.fillWidth: true
                spacing: Theme.s1
                Repeater {
                    model: RoleFilter { sourceModel: App.keybinds; role: "category"; value: group.modelData }
                    delegate: ShortcutRow {
                        filter: page.filter
                        Layout.fillWidth: true
                        onShownChanged: rows.visibleCount += shown ? 1 : -1
                        Component.onCompleted: if (shown) rows.visibleCount++
                        onAskConflict: (id, seq, names) => { conflict.action = id; conflict.seq = seq; conflict.names = names; conflict.open() }
                        onAskWarning: (id, seq, text) => { warning.action = id; warning.seq = seq; warning.text = text; warning.open() }
                    }
                }
            }
        }
    }

    ConflictSheet { id: conflict }

    Sheet {
        id: warning
        property string action: ""
        property string seq: ""
        property string text: ""
        title: qsTr("This shortcut puts your real mic on air")
        message: text
        iconName: "shield-alert"
        iconTint: Theme.live
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: qsTr("A red MIC LIVE badge appears on screen and a tone plays whenever it's on. You can change this in Audio & mic.")
        }
        footer: [
            PillButton {
                text: qsTr("I understand, set it")
                kind: "live"
                onClicked: {
                    const clash = App.keybinds.bind(warning.action, warning.seq, false)
                    warning.close()
                    if (clash.length > 0) { conflict.action = warning.action; conflict.seq = warning.seq; conflict.names = clash; conflict.open() }
                }
            },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: warning.close() }
        ]
    }

    Sheet {
        id: resetAll
        title: qsTr("Reset every shortcut?")
        message: qsTr("All actions go back to their original keys. Phrase and sound shortcuts stay as they are.")
        iconName: "rotate-ccw"
        footer: [
            PillButton { text: qsTr("Reset all"); kind: "danger"; onClicked: { App.keybinds.resetAll(); resetAll.close() } },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: resetAll.close() }
        ]
    }
}
