import QtQuick
import QtQuick.Controls.Basic

ScrollBar {
    id: bar
    hoverEnabled: true
    minimumSize: 0.08
    padding: 3
    contentItem: Rectangle {
        implicitWidth: bar.hovered || bar.pressed ? 8 : 5
        implicitHeight: implicitWidth
        radius: width / 2
        color: bar.pressed ? Theme.muted : Theme.alpha(Theme.text, bar.hovered ? 0.35 : 0.2)
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active || bar.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.fast } }
    }
}
