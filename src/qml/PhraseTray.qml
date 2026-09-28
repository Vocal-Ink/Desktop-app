import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// One-tap phrases under the composer, filtered by category.
ColumnLayout {
    id: tray
    signal edit()
    property string category: ""

    spacing: Theme.s2

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s1
        Repeater {
            model: [""].concat(App.phrases.categories)
            AbstractButton {
                id: tab
                required property string modelData
                readonly property bool selected: tray.category === modelData
                implicitHeight: Math.round(26 * Math.min(1.4, Theme.scale))
                implicitWidth: label.implicitWidth + Theme.s3 * 2
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: label.text
                Accessible.checked: selected
                onClicked: tray.category = modelData
                contentItem: Text {
                    id: label
                    text: tab.modelData === "" ? qsTr("All") : tab.modelData
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fsSm
                    font.weight: Font.DemiBold
                    color: tab.selected ? Theme.text : tab.hovered ? Theme.text : Theme.faint
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Item {
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: tab.selected ? parent.width - Theme.s3 : 0
                        height: 2
                        radius: 1
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                    }
                    FocusFrame { shown: tab.visualFocus; baseRadius: Theme.radiusSm }
                }
            }
        }
        Item { Layout.fillWidth: true }
        PillButton {
            kind: "ghost"
            small: true
            text: qsTr("Edit phrases")
            iconName: "square-pen"
            onClicked: tray.edit()
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        implicitHeight: Math.round(Theme.controlSm * 0.92)
        orientation: ListView.Horizontal
        spacing: 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: App.phrases
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Quick phrases")
        delegate: Item {
            id: cell
            required property var model
            readonly property bool shown: tray.category === "" || tray.category === model.category
            width: shown ? chip.implicitWidth + Theme.s2 : 0
            height: chip.implicitHeight
            visible: shown
            Chip {
                id: chip
                text: cell.model.text
                shortcut: cell.model.hotkey
                tint: cell.model.tint !== "" ? cell.model.tint : "transparent"
                tip: cell.model.category
                onClicked: App.trigger("phrase:" + cell.model.phraseIndex)
            }
        }
    }
}
