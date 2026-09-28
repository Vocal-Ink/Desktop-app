import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// A phrase on the board. Tap to say it; the tile floods with ink as it goes.
Item {
    id: cell
    required property int index
    required property string text
    required property string hotkey
    required property string category
    required property string tint
    required property int phraseIndex
    property bool scanned: false
    signal edit()

    AbstractButton {
        id: tile
        anchors.fill: parent
        anchors.margins: Theme.s1 + 1
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: cell.text
        Accessible.description: cell.hotkey !== "" ? qsTr("Shortcut %1").arg(App.nativeShortcut(cell.hotkey)) : ""
        onClicked: { App.trigger("phrase:" + cell.phraseIndex); flood.restart() }
        Keys.onMenuPressed: cell.edit()

        background: Rectangle {
            radius: Theme.radiusLg
            color: tile.down ? Theme.mix(Theme.raised, Theme.accent, 0.12) : Theme.raised
            border.color: cell.scanned ? Theme.focus : tile.hovered ? Theme.accent : Theme.line
            border.width: cell.scanned ? Theme.focusWidth + 1 : Theme.hairline
            clip: true
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }

            // Category colour, as a nib mark.
            Rectangle {
                visible: cell.tint !== ""
                x: Theme.s4
                y: Theme.s3
                width: 18
                height: 4
                radius: 2
                color: cell.tint || "transparent"
            }
            // The ink flood when it's said.
            Rectangle {
                id: ink
                height: parent.height
                width: 0
                color: Theme.accentWashStrong
                radius: parent.radius
            }
            SequentialAnimation {
                id: flood
                NumberAnimation { target: ink; property: "width"; from: 0; to: tile.width; duration: Theme.motionOn ? 420 : 0; easing.type: Easing.OutCubic }
                NumberAnimation { target: ink; property: "opacity"; from: 1; to: 0; duration: Theme.motionOn ? 380 : 0 }
                PropertyAction { target: ink; property: "width"; value: 0 }
                PropertyAction { target: ink; property: "opacity"; value: 1 }
            }
            FocusFrame { shown: tile.visualFocus; baseRadius: parent.radius }
        }

        contentItem: ColumnLayout {
            spacing: Theme.s2
            Item { Layout.preferredHeight: Theme.s2 }
            Txt {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: cell.text
                font.family: Theme.stageFont
                font.weight: Font.DemiBold
                font.pixelSize: Theme.fsLg
                elide: Text.ElideRight
                maximumLineCount: 3
                leftPadding: Theme.s2
                rightPadding: Theme.s2
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s2
                Layout.rightMargin: Theme.s1
                spacing: Theme.s2
                KeyCombo { sequence: cell.hotkey; compact: true }
                Item { Layout.fillWidth: true }
                IconButton {
                    small: true
                    iconName: "square-pen"
                    tip: qsTr("Edit “%1”").arg(cell.text)
                    opacity: tile.hovered || activeFocus || tile.visualFocus ? 1 : 0
                    implicitWidth: Math.round(30 * Theme.scale)
                    onClicked: cell.edit()
                }
            }
        }
    }
}
