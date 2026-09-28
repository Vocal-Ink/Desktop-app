import QtQuick
import QtQuick.Controls.Basic

// A small status readout with a dot: ok | warn | live | idle | accent. Clickable.
AbstractButton {
    id: root
    property string tone: "idle"
    property string iconName: ""
    property string tip: ""
    property bool pulse: false

    readonly property color toneColor: tone === "ok" ? Theme.ok : tone === "warn" ? Theme.warn : tone === "live" ? Theme.live
                                     : tone === "accent" ? Theme.accentText : Theme.faint

    implicitHeight: Math.round(30 * Math.min(1.5, Theme.scale))
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
    Accessible.description: tip

    contentItem: Row {
        spacing: Theme.s2
        Item {
            width: 8
            height: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName === ""
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: root.toneColor
            }
            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: "transparent"
                border.color: root.toneColor
                border.width: 2
                visible: root.pulse && Theme.motionOn
                NumberAnimation on scale {
                    running: root.pulse && Theme.motionOn
                    loops: Animation.Infinite
                    from: 1; to: 2.6; duration: 1100; easing.type: Easing.OutCubic
                }
                NumberAnimation on opacity {
                    running: root.pulse && Theme.motionOn
                    loops: Animation.Infinite
                    from: 0.9; to: 0; duration: 1100
                }
            }
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.iconName !== ""
            name: root.iconName
            color: root.toneColor
            size: Math.round(15 * Theme.scale)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: root.tone === "live" ? Theme.live : Theme.text
        }
    }
    background: Rectangle {
        radius: Theme.corners === "sharp" ? 3 : height / 2
        color: root.tone === "live" ? Theme.liveWash : root.hovered ? Theme.hover : "transparent"
        border.color: root.tone === "live" ? Theme.alpha(Theme.live, 0.5) : Theme.line
        border.width: Theme.hairline
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }
    InkTip { visible: root.tip !== "" && root.hovered; text: root.tip }
}
