import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Ink.Core

// Full screen text for talking in person: hold up the laptop, or flip the
// text to face the person across the table.
Window {
    id: win
    title: qsTr("Show text")
    color: Theme.bg
    flags: Qt.Window
    property bool flipped: false

    function open() {
        if (typeof smokeTest !== "undefined" && smokeTest) {
            width = 1200; height = 760
            show()
        } else {
            showFullScreen()
        }
        raise()
        requestActivate()
        composer.focusEditor()
    }

    Shortcut { sequence: "Esc"; onActivated: win.close() }
    Shortcut { sequence: "F11"; onActivated: win.close() }
    Shortcut { sequence: "Ctrl+F"; onActivated: win.flipped = !win.flipped }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s8
        spacing: Theme.s5

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            Image { source: "qrc:/icons/tray.png"; sourceSize: Qt.size(64, 64); Layout.preferredWidth: Math.round(28 * Theme.scale); Layout.preferredHeight: Layout.preferredWidth }
            Txt { text: qsTr("I'm using Vocal Ink to talk. Please read along."); role: "caption"; Layout.fillWidth: true }
            PillButton {
                kind: win.flipped ? "primary" : "secondary"
                iconName: "rotate-ccw"
                text: win.flipped ? qsTr("Facing them") : qsTr("Flip for the person opposite")
                shortcut: "Ctrl+F"
                onClicked: win.flipped = !win.flipped
            }
            PillButton { kind: "ghost"; iconName: "minimize-2"; text: qsTr("Exit"); shortcut: "Esc"; onClicked: win.close() }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            rotation: win.flipped ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: Theme.slow; easing.type: Easing.InOutCubic } }

            InkLine {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: App.currentLine !== "" ? App.currentLine : qsTr("Type below. Your words appear here, big.")
                progress: App.currentLine !== "" ? App.lineProgress : 1
                animate: App.currentLine !== ""
                fontSize: Math.max(Theme.stageSize * 1.8, Math.min(win.width / 14, 120))
                fontFamily: Theme.stageFont
            }
        }

        Composer {
            id: composer
            Layout.fillWidth: true
            Layout.maximumWidth: Math.round(900 * Theme.scale)
            Layout.alignment: Qt.AlignHCenter
            compact: true
        }
    }
}
