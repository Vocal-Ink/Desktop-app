import QtQuick

// The keyboard focus ring. Drawn outside the control so it never covers content.
Rectangle {
    property bool shown: false
    property real baseRadius: Theme.radius

    anchors.fill: parent
    anchors.margins: -(Theme.focusWidth + 2)
    radius: baseRadius + Theme.focusWidth + 2
    color: "transparent"
    border.color: Theme.focus
    border.width: Theme.focusWidth
    visible: shown
    z: 10
}
