import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// A soundboard pad. Click plays (click again stops); right-click edits.
Item {
    id: cell
    required property string soundId
    required property string name
    required property string hotkey
    required property double gain
    required property string tint
    required property bool playing
    signal edit()

    AbstractButton {
        id: pad
        anchors.fill: parent
        anchors.margins: Theme.s1 + 1
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: cell.name + (cell.playing ? qsTr(", playing") : "")
        onClicked: App.sounds.play(cell.soundId)

        TapHandler { acceptedButtons: Qt.RightButton; onTapped: cell.edit() }

        readonly property color padColor: cell.tint !== "" ? cell.tint : Theme.accent

        background: Rectangle {
            radius: Theme.radiusLg
            color: cell.playing ? Theme.alpha(pad.padColor, 0.3) : pad.down ? Theme.alpha(pad.padColor, 0.2) : Theme.raised
            border.color: cell.playing || pad.hovered ? pad.padColor : Theme.line
            border.width: cell.playing ? 2 : Theme.hairline
            Behavior on color { ColorAnimation { duration: Theme.fast } }

            // A colour band like the lip of a drum pad.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.s3
                height: 4
                radius: 2
                color: pad.padColor
                opacity: cell.playing ? 1 : 0.7
            }
            FocusFrame { shown: pad.visualFocus; baseRadius: parent.radius }
        }

        contentItem: ColumnLayout {
            spacing: Theme.s1
            RowLayout {
                Layout.fillWidth: true
                Icon {
                    name: cell.playing ? "audio-lines" : "play"
                    color: cell.playing ? pad.padColor : Theme.muted
                    SequentialAnimation on opacity {
                        running: cell.playing && Theme.motionOn
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.4; duration: 380 }
                        NumberAnimation { to: 1; duration: 380 }
                    }
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    small: true
                    iconName: "square-pen"
                    tip: qsTr("Edit “%1”").arg(cell.name)
                    implicitWidth: Math.round(28 * Theme.scale)
                    opacity: pad.hovered || pad.visualFocus ? 1 : 0
                    onClicked: cell.edit()
                }
            }
            Item { Layout.fillHeight: true }
            Txt {
                Layout.fillWidth: true
                text: cell.name
                role: "label"
                font.pixelSize: Theme.fsLg
                elide: Text.ElideRight
                maximumLineCount: 2
            }
            KeyCombo { sequence: cell.hotkey; compact: true }
            Item { Layout.preferredHeight: Theme.s2 }
        }
    }
}
