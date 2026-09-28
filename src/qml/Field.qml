import QtQuick
import QtQuick.Controls.Basic

// Single-line text input.
TextField {
    id: root
    property string label: ""
    property string iconName: ""
    property bool secret: false

    implicitHeight: Theme.control
    implicitWidth: 280
    leftPadding: iconName !== "" ? Theme.s3 + Math.round(18 * Theme.scale) + Theme.s2 : Theme.s3
    rightPadding: Theme.s3
    font.family: Theme.uiFont
    font.pixelSize: Theme.fsMd
    font.letterSpacing: Theme.tracking(Theme.fsMd)
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accentWashStrong
    selectedTextColor: Theme.text
    echoMode: secret ? TextInput.Password : TextInput.Normal
    selectByMouse: true
    hoverEnabled: true
    Accessible.name: label !== "" ? label : placeholderText

    background: Rectangle {
        radius: Theme.radius
        color: Theme.sunken
        border.width: root.activeFocus ? 2 : Theme.hairline
        border.color: root.activeFocus ? Theme.accent : root.hovered ? Theme.mix(Theme.line, Theme.text, 0.2) : Theme.line
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        Icon {
            visible: root.iconName !== ""
            name: root.iconName
            size: Math.round(18 * Theme.scale)
            color: Theme.faint
            anchors.left: parent.left
            anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
