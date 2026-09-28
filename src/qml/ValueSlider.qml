import QtQuick
import QtQuick.Controls.Basic

// Slider with the current value shown to its right. `suffix` like "%" or " dB".
Slider {
    id: root
    property string suffix: ""
    property string label: ""
    property bool showValue: true
    property var format: null // optional function(value) -> string

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    stepSize: 1
    snapMode: Slider.SnapAlways
    implicitWidth: 260
    implicitHeight: Math.max(Theme.controlSm, handle.height)
    rightPadding: showValue ? valueText.width + Theme.s3 : 0
    Accessible.name: label
    Accessible.description: valueText.text

    readonly property string displayValue: format ? format(value) : Math.round(value) + suffix

    background: Item {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: Math.round(6 * Math.min(1.5, Theme.scale))

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.mix(Theme.sunken, Theme.text, 0.08)
            border.width: Theme.highContrast ? 1 : 0
            border.color: Theme.line
        }
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: root.enabled ? Theme.accent : Theme.faint
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: Math.round((Theme.largeTargets ? 26 : 20) * Math.min(1.5, Theme.scale))
        implicitHeight: implicitWidth
        radius: Theme.corners === "sharp" ? 3 : width / 2
        color: Theme.raised
        border.color: root.enabled ? Theme.accent : Theme.faint
        border.width: root.pressed ? 6 : 4
        Behavior on border.width { NumberAnimation { duration: Theme.fast } }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }

    Text {
        id: valueText
        visible: root.showValue
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(implicitWidth, Math.round(48 * Theme.scale))
        horizontalAlignment: Text.AlignRight
        text: root.displayValue
        font.family: Theme.monoFont
        font.pixelSize: Theme.fsSm
        color: Theme.muted
    }
}
