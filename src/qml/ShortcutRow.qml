import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// One action and its shortcut.
Item {
    id: row
    required property string actionId
    required property string title
    required property string description
    required property string shortcut
    required property bool isDefault
    required property bool isGlobal
    required property bool hold
    required property string warning
    required property string defaultShortcut
    property string filter: ""
    signal askConflict(string id, string seq, var names)
    signal askWarning(string id, string seq, string text)

    readonly property bool shown: filter === "" || (title + " " + description).toLowerCase().indexOf(filter) >= 0
    visible: shown
    implicitHeight: shown ? line.implicitHeight + Theme.s2 * 2 : 0

    HoverHandler { id: hover }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: hover.hovered ? Theme.hover : "transparent"
    }

    RowLayout {
        id: line
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.s2
        anchors.rightMargin: Theme.s2
        spacing: Theme.s3

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                spacing: Theme.s2
                Txt { text: row.title; role: "label" }
                Rectangle {
                    visible: row.hold
                    implicitWidth: holdText.implicitWidth + Theme.s2 * 2
                    implicitHeight: holdText.implicitHeight + 2
                    radius: height / 2
                    color: Theme.accentWash
                    Text { id: holdText; anchors.centerIn: parent; text: qsTr("hold"); font.family: Theme.uiFont; font.pixelSize: Theme.fsXs; font.weight: Font.Bold; color: Theme.accentText }
                }
                Rectangle {
                    visible: !row.isGlobal
                    implicitWidth: localText.implicitWidth + Theme.s2 * 2
                    implicitHeight: localText.implicitHeight + 2
                    radius: height / 2
                    color: Theme.hover
                    Text { id: localText; anchors.centerIn: parent; text: qsTr("in app"); font.family: Theme.uiFont; font.pixelSize: Theme.fsXs; font.weight: Font.Bold; color: Theme.muted }
                }
                Icon { visible: row.warning !== ""; name: "shield-alert"; color: Theme.live; size: Math.round(14 * Theme.scale) }
            }
            Txt { text: row.description; role: "caption"; Layout.fillWidth: true }
        }
        IconButton {
            visible: !row.isDefault
            small: true
            iconName: "rotate-ccw"
            tip: row.defaultShortcut !== "" ? qsTr("Back to %1").arg(App.nativeShortcut(row.defaultShortcut)) : qsTr("Back to no shortcut")
            onClicked: App.keybinds.reset(row.actionId)
        }
        ShortcutField {
            label: row.title
            sequence: row.shortcut
            onRecorded: (seq) => {
                if (row.warning !== "") {
                    row.askWarning(row.actionId, seq, row.warning)
                    return
                }
                const clash = App.keybinds.bind(row.actionId, seq, false)
                if (clash.length > 0)
                    row.askConflict(row.actionId, seq, clash)
            }
            onCleared: App.keybinds.clear(row.actionId)
        }
    }
}
