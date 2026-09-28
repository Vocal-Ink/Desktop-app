import QtQuick
import QtQuick.Controls.Basic

// One destination in the sidebar.
AbstractButton {
    id: root
    property string iconName
    property bool current: false
    property bool labels: true
    property string shortcut: ""
    property int badge: 0

    implicitHeight: Math.round(Theme.control * 1.02)
    implicitWidth: 200
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: current

    contentItem: Item {
        Icon {
            id: icon
            x: root.labels ? Theme.s3 : (parent.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: Math.round(20 * Theme.scale)
            color: root.current ? Theme.accentText : root.hovered ? Theme.text : Theme.muted
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
        Text {
            visible: root.labels
            anchors.left: icon.right
            anchors.leftMargin: Theme.s3
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: Theme.fsMd
            font.weight: root.current ? Font.Bold : Font.DemiBold
            font.letterSpacing: Theme.tracking(Theme.fsMd)
            color: root.current ? Theme.text : root.hovered ? Theme.text : Theme.muted
        }
        Rectangle {
            visible: root.badge > 0
            anchors.right: parent.right
            anchors.rightMargin: root.labels ? Theme.s3 : 6
            anchors.top: root.labels ? undefined : parent.top
            anchors.topMargin: 6
            anchors.verticalCenter: root.labels ? parent.verticalCenter : undefined
            width: 8
            height: 8
            radius: 4
            color: Theme.live
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: root.current ? Theme.accentWash : root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }

        // A short brush stroke marks where you are.
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: -Theme.s2
            width: 4
            height: root.current ? parent.height * 0.56 : 0
            radius: 2
            color: Theme.accent
            Behavior on height { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack } }
        }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }

    InkTip { visible: !root.labels && root.hovered; text: root.text }
}
