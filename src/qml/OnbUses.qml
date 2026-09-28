import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    id: step
    title: qsTr("Where will you talk?")
    lead: qsTr("Pick all that apply. It decides which steps come next; nothing is locked in.")

    property var chosen: (App.prefs["ui/uses"] || []).slice()
    function toggle(id) {
        const i = chosen.indexOf(id)
        const c = chosen.slice()
        if (i >= 0) c.splice(i, 1); else c.push(id)
        chosen = c
    }
    function commit() { App.prefs["ui/uses"] = chosen }

    GridLayout {
        Layout.fillWidth: true
        columns: width > Math.round(640 * Theme.scale) ? 3 : width > Math.round(400 * Theme.scale) ? 2 : 1
        columnSpacing: Theme.s3
        rowSpacing: Theme.s3
        Repeater {
            model: [
                { id: "calls", icon: "phone", title: qsTr("Calls & meetings"), text: qsTr("Discord, Zoom, Teams, Meet") },
                { id: "games", icon: "gamepad-2", title: qsTr("Games"), text: qsTr("Voice chat while you play") },
                { id: "stream", icon: "radio", title: qsTr("Streaming"), text: qsTr("Captions, OBS, reading chat") },
                { id: "inperson", icon: "users-round", title: qsTr("In person"), text: qsTr("Out loud, and text people can read") },
                { id: "work", icon: "presentation", title: qsTr("Work & school"), text: qsTr("Presentations, classes, desks") },
                { id: "home", icon: "house", title: qsTr("Everyday"), text: qsTr("Family, friends, around the house") }
            ]
            AbstractButton {
                id: opt
                required property var modelData
                readonly property bool on: step.chosen.indexOf(modelData.id) >= 0
                Layout.fillWidth: true
                implicitHeight: Math.round(104 * Math.min(1.4, Theme.scale))
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                checkable: true
                checked: on
                Accessible.role: Accessible.CheckBox
                Accessible.name: modelData.title
                Accessible.description: modelData.text
                Accessible.checked: on
                onClicked: step.toggle(modelData.id)
                contentItem: ColumnLayout {
                    spacing: Theme.s1
                    RowLayout {
                        Layout.fillWidth: true
                        Icon { name: opt.modelData.icon; color: opt.on ? Theme.accentText : Theme.muted; size: Math.round(22 * Theme.scale) }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: Math.round(20 * Theme.scale); height: width
                            radius: Theme.corners === "sharp" ? 3 : 6
                            color: opt.on ? Theme.accent : "transparent"
                            border.color: opt.on ? Theme.accent : Theme.faint
                            border.width: 2
                            Icon { anchors.centerIn: parent; visible: opt.on; name: "check"; strokeWidth: 3; size: parent.width * 0.7; color: Theme.accentInk }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Txt { text: opt.modelData.title; role: "label"; font.pixelSize: Theme.fsLg }
                    Txt { text: opt.modelData.text; role: "caption"; Layout.fillWidth: true }
                }
                padding: Theme.s4
                background: Rectangle {
                    radius: Theme.radiusLg
                    color: opt.on ? Theme.accentWash : opt.hovered ? Theme.mix(Theme.surface, Theme.text, 0.04) : Theme.surface
                    border.color: opt.on ? Theme.accent : Theme.line
                    border.width: opt.on ? 2 : Theme.hairline
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                    FocusFrame { shown: opt.visualFocus; baseRadius: parent.radius }
                }
            }
        }
    }
}
