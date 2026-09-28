import QtQuick

// A thin horizontal level meter (mic test, routing check).
Rectangle {
    id: bar
    property real level: 0
    property color tint: Theme.accent

    implicitWidth: 200
    implicitHeight: 6
    radius: height / 2
    color: Theme.mix(Theme.sunken, Theme.text, 0.08)
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Level")

    Rectangle {
        height: parent.height
        radius: parent.radius
        width: Math.max(height, parent.width * Math.min(1, Math.pow(bar.level, 0.6)))
        color: bar.tint
        opacity: bar.level > 0.01 ? 1 : 0.35
        Behavior on width { NumberAnimation { duration: 60 } }
    }
}
