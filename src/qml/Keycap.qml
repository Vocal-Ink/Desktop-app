import QtQuick

// One key of a shortcut, drawn like the physical key.
Rectangle {
    id: cap
    property string label
    property bool dim: false

    implicitWidth: Math.max(implicitHeight, t.implicitWidth + Theme.s2 * 2)
    implicitHeight: Math.round(22 * Theme.scale)
    radius: Math.min(6, Theme.radiusSm)
    color: dim ? Theme.alpha("#000000", 0.18) : Theme.sunken
    border.color: dim ? Theme.alpha("#FFFFFF", 0.25) : Theme.line
    border.width: 1
    Accessible.ignored: true

    // The key's lower lip.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        radius: parent.radius
        color: cap.dim ? Theme.alpha("#000000", 0.2) : Theme.line
    }
    Text {
        id: t
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -1
        text: cap.label
        font.family: Theme.monoFont
        font.pixelSize: Math.round(12 * Theme.scale)
        font.weight: Font.Medium
        color: cap.dim ? Theme.accentInk : Theme.muted
    }
}
