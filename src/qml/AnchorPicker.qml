import QtQuick
import QtQuick.Controls.Basic
import Ink.Core

// Where on the screen something sits: a 3×3 grid of spots.
Grid {
    id: root
    property string value: "bottom"
    property string label: ""
    signal picked(string anchor)

    columns: 3
    spacing: 3
    Accessible.role: Accessible.Grouping
    Accessible.name: label

    readonly property var spots: ["top-left", "top", "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right"]
    function spotName(s) {
        switch (s) {
        case "top-left": return qsTr("Top left")
        case "top": return qsTr("Top")
        case "top-right": return qsTr("Top right")
        case "left": return qsTr("Left")
        case "center": return qsTr("Centre")
        case "right": return qsTr("Right")
        case "bottom-left": return qsTr("Bottom left")
        case "bottom": return qsTr("Bottom")
        case "bottom-right": return qsTr("Bottom right")
        }
        return s
    }

    Repeater {
        model: root.spots
        AbstractButton {
            id: spot
            required property string modelData
            readonly property bool on: root.value === modelData
            width: Math.round(30 * Theme.scale)
            height: Math.round(20 * Theme.scale)
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            checkable: true
            checked: on
            Accessible.role: Accessible.RadioButton
            Accessible.name: root.spotName(modelData)
            Accessible.checked: on
            onClicked: root.picked(modelData)
            background: Rectangle {
                radius: 4
                color: spot.on ? Theme.accent : spot.hovered ? Theme.hover : Theme.sunken
                border.color: spot.on ? Theme.accent : Theme.line
                border.width: Theme.hairline
                FocusFrame { shown: spot.visualFocus; baseRadius: parent.radius }
            }
        }
    }
}
