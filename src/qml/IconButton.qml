import QtQuick
import QtQuick.Controls.Basic

// A square icon-only button. Always carries an accessible name (tip).
AbstractButton {
    id: root
    property string iconName
    property string tip
    property color tint: Theme.muted
    property bool active: false
    property real iconSize: Math.round(18 * Theme.scale)
    property bool small: false

    implicitWidth: small ? Theme.controlSm : Theme.control
    implicitHeight: implicitWidth
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: tip

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: root.iconSize
            color: !root.enabled ? Theme.faint : root.active ? Theme.accentText : root.hovered ? Theme.text : root.tint
        }
    }
    background: Rectangle {
        radius: Theme.corners === "round" ? width / 2 : Theme.radius
        color: root.active ? Theme.accentWash : root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }
    InkTip { visible: root.hovered && root.tip !== ""; text: root.tip }
}
