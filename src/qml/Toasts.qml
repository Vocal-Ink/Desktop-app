import QtQuick
import QtQuick.Layouts
import Ink.Core

// Short notices in the bottom-right corner. Screen readers hear them too.
Item {
    id: host
    width: Math.round(380 * Math.min(1.4, Theme.scale))
    height: col.implicitHeight

    function show(message, level) {
        if (!message)
            return
        // Repeated notices refresh instead of stacking.
        for (let i = 0; i < toasts.count; ++i) {
            if (toasts.get(i).message === message) {
                toasts.setProperty(i, "stamp", Date.now())
                return
            }
        }
        toasts.insert(0, { message: message, level: level || 0, stamp: Date.now() })
        if (toasts.count > 4)
            toasts.remove(4)
        App.announce(message, level >= 2)
    }

    ListModel { id: toasts }

    Column {
        id: col
        width: parent.width
        spacing: Theme.s2

        move: Transition { NumberAnimation { properties: "y"; duration: Theme.normal; easing.type: Easing.OutCubic } }
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal }
            NumberAnimation { property: "x"; from: Theme.travel(24); to: 0; duration: Theme.normal; easing.type: Easing.OutCubic }
        }

        Repeater {
            model: toasts
            Rectangle {
                id: toast
                required property int index
                required property string message
                required property int level
                required property double stamp
                width: col.width
                implicitHeight: row.implicitHeight + Theme.s3 * 2
                radius: Theme.radius
                color: Theme.raised
                border.color: level >= 2 ? Theme.alpha(Theme.live, 0.6) : level === 1 ? Theme.alpha(Theme.warn, 0.55) : Theme.line
                border.width: Theme.hairline
                Accessible.role: Accessible.AlertMessage
                Accessible.name: message

                // Ink edge in the level colour.
                Rectangle {
                    width: 3
                    radius: 2
                    anchors.left: parent.left
                    anchors.top: parent.top
                                anchors.margins: 6
                    color: toast.level >= 2 ? Theme.live : toast.level === 1 ? Theme.warn : Theme.accent
                }

                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.margins: Theme.s3
                    anchors.leftMargin: Theme.s4
                    spacing: Theme.s3
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        name: toast.level >= 2 ? "circle-alert" : toast.level === 1 ? "triangle-alert" : "info"
                        color: toast.level >= 2 ? Theme.live : toast.level === 1 ? Theme.warn : Theme.accentText
                        size: Math.round(18 * Theme.scale)
                    }
                    Txt {
                        text: toast.message
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fsSm
                    }
                    IconButton {
                        Layout.alignment: Qt.AlignTop
                        small: true
                        iconName: "x"
                        tip: qsTr("Dismiss")
                        iconSize: Math.round(14 * Theme.scale)
                        implicitWidth: Math.round(26 * Theme.scale)
                        onClicked: toasts.remove(toast.index)
                    }
                }

                Timer {
                    running: true
                    interval: toast.level >= 2 ? 9000 : 5000
                    onTriggered: {
                        if (Date.now() - toast.stamp >= interval - 50) {
                            if (toast.index >= 0 && toast.index < toasts.count)
                                toasts.remove(toast.index)
                        } else {
                            restart()
                        }
                    }
                }
            }
        }
    }
}
