import QtQuick
import QtQuick.Controls.Basic

// On/off switch. The text (if any) sits to the left and is the accessible name.
Switch {
    id: root
    property string tip: ""

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    spacing: Theme.s3
    font.family: Theme.uiFont
    font.pixelSize: Theme.fsMd
    Accessible.name: text !== "" ? text : tip

    readonly property real trackW: Math.round((Theme.largeTargets ? 52 : 44) * Math.min(1.4, Theme.scale))
    readonly property real trackH: Math.round(trackW * 0.58)

    indicator: Rectangle {
        x: root.width - width - root.rightPadding
        y: (root.height - height) / 2
        implicitWidth: root.trackW
        implicitHeight: root.trackH
        radius: Theme.corners === "sharp" ? 3 : height / 2
        color: root.checked ? (root.hovered ? Theme.accentHover : Theme.accent)
                            : (root.hovered ? Theme.mix(Theme.sunken, Theme.text, 0.12) : Theme.mix(Theme.sunken, Theme.text, 0.06))
        border.width: root.checked ? 0 : Theme.hairline
        border.color: Theme.line
        opacity: root.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        Rectangle {
            readonly property real inset: 3
            width: parent.height - inset * 2
            height: width
            radius: Theme.corners === "sharp" ? 2 : width / 2
            y: inset
            x: root.checked ? parent.width - width - inset : inset
            color: root.checked ? Theme.accentInk : Theme.muted
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }

    contentItem: Txt {
        text: root.text
        visible: root.text !== ""
        rightPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        color: root.enabled ? Theme.text : Theme.faint
    }
    implicitHeight: Math.max(trackH, contentItem.implicitHeight)
}
