import QtQuick
import QtQuick.Controls.Basic

// A colour choice. An empty colour means "none".
AbstractButton {
    id: root
    property string swatch: ""
    property bool selected: false
    property string name: ""

    implicitWidth: Math.round(30 * Math.min(1.5, Theme.scale))
    implicitHeight: implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    checkable: true
    checked: selected
    Accessible.role: Accessible.RadioButton
    Accessible.name: name !== "" ? name : swatch === "" ? qsTr("No colour") : swatch
    Accessible.checked: selected

    background: Rectangle {
        radius: Theme.corners === "sharp" ? 3 : width / 2
        color: root.swatch === "" ? "transparent" : root.swatch
        border.color: root.selected ? Theme.text : root.swatch === "" ? Theme.line : Theme.alpha(Theme.text, 0.15)
        border.width: root.selected ? 3 : 1
        scale: root.hovered ? 1.08 : 1
        Behavior on scale { NumberAnimation { duration: Theme.fast } }
        // "None" is a slashed circle.
        Rectangle {
            visible: root.swatch === ""
            anchors.centerIn: parent
            width: parent.width * 0.7
            height: 2
            rotation: -45
            color: Theme.faint
        }
        Icon {
            visible: root.selected && root.swatch !== ""
            anchors.centerIn: parent
            name: "check"
            size: parent.width * 0.55
            color: Theme.luminance(root.swatch) > 0.45 ? "#140E24" : "#FFFFFF"
            strokeWidth: 3
        }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }
    InkTip { visible: root.hovered && root.name !== ""; text: root.name }
}
