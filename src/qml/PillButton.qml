import QtQuick
import QtQuick.Controls.Basic

// The app's button. kind: primary | secondary | ghost | danger | live
Button {
    id: root
    property string kind: "secondary"
    property string iconName: ""
    property string shortcut: ""   // portable key sequence shown as keycaps
    property bool small: false
    property string tip: ""

    implicitHeight: small ? Theme.controlSm : Theme.control
    leftPadding: text === "" ? 0 : (small ? Theme.s3 : Theme.s4)
    rightPadding: text === "" ? 0 : (small ? Theme.s3 : Theme.s4)
    implicitWidth: text === "" ? implicitHeight : Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.uiFont
    font.pixelSize: small ? Theme.fsSm : Theme.fsMd
    font.weight: Font.DemiBold
    font.letterSpacing: Theme.tracking(small ? Theme.fsSm : Theme.fsMd)
    Accessible.name: text !== "" ? text : tip
    Accessible.description: tip

    readonly property color fg: !enabled ? Theme.faint
                              : kind === "primary" ? Theme.accentInk
                              : kind === "live" ? "#FFFFFF"
                              : kind === "danger" ? Theme.live
                              : kind === "ghost" ? (hovered ? Theme.text : Theme.muted)
                              : Theme.text

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        Row {
            id: content
            anchors.centerIn: parent
            spacing: Theme.s2
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.iconName
                visible: root.iconName !== ""
                color: root.fg
                size: Math.round((root.small ? 16 : 18) * Theme.scale)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                visible: root.text !== ""
                font: root.font
                color: root.fg
            }
            KeyCombo {
                anchors.verticalCenter: parent.verticalCenter
                sequence: root.shortcut
                visible: root.shortcut !== ""
                dim: root.kind === "primary"
            }
        }
    }

    background: Rectangle {
        radius: Theme.corners === "round" ? height / 2 : Theme.radius
        color: {
            if (!root.enabled)
                return root.kind === "ghost" ? "transparent" : Theme.surface
            switch (root.kind) {
            case "primary": return root.down ? Theme.accentPressed : root.hovered ? Theme.accentHover : Theme.accent
            case "live": return root.down ? Qt.darker(Theme.live, 1.15) : Theme.live
            case "danger": return root.down ? Theme.alpha(Theme.live, 0.3) : root.hovered ? Theme.alpha(Theme.live, 0.22) : Theme.liveWash
            case "ghost": return root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
            default: return root.down ? Theme.mix(Theme.raised, Theme.text, 0.1) : root.hovered ? Theme.mix(Theme.raised, Theme.text, 0.05) : Theme.raised
            }
        }
        border.width: root.kind === "secondary" || Theme.highContrast ? Theme.hairline : 0
        border.color: root.kind === "danger" ? Theme.alpha(Theme.live, 0.5) : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }

    scale: down && Theme.motionOn ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }

    InkTip { visible: root.tip !== "" && root.hovered; text: root.tip }
}
