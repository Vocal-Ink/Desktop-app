import QtQuick
import QtQuick.Controls.Basic

// Compact pill for suggestions, filters and quick phrases.
AbstractButton {
    id: root
    property string iconName: ""
    property color tint: "transparent"   // optional category colour
    property bool selected: false
    property string shortcut: ""
    property string tip: ""

    implicitHeight: Math.round(Theme.controlSm * 0.92)
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    leftPadding: Theme.s3
    rightPadding: Theme.s3
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.uiFont
    font.pixelSize: Theme.fsSm
    font.weight: Font.DemiBold
    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Row {
        spacing: Theme.s1 + 2
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.tint.a > 0
            width: 8
            height: 8
            radius: 4
            color: root.tint
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName !== ""
            name: root.iconName
            size: Math.round(15 * Theme.scale)
            color: root.selected ? Theme.accentInk : Theme.muted
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: root.selected ? Theme.accentInk : Theme.text
            elide: Text.ElideRight
        }
        KeyCombo {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.shortcut !== ""
            sequence: root.shortcut
            dim: root.selected
            compact: true
        }
    }
    background: Rectangle {
        radius: Theme.corners === "sharp" ? 3 : height / 2
        color: root.selected ? Theme.accent
             : root.down ? Theme.pressed
             : root.hovered ? Theme.mix(Theme.raised, Theme.text, 0.06) : Theme.raised
        border.color: root.selected ? Theme.accent : Theme.line
        border.width: Theme.hairline
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }
    InkTip { visible: root.tip !== "" && root.hovered; text: root.tip }
}
