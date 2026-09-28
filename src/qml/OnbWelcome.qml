import QtQuick
import QtQuick.Layouts
import Ink.Core

// Welcome: the app introduces itself by writing in ink.
ColumnLayout {
    property bool canContinue: true
    property string continueLabel: ""
    function commit() {}

    spacing: Theme.s6

    InkLine {
        id: hello
        Layout.fillWidth: true
        text: qsTr("Hello. From now on, I'll say what you write.")
        fontFamily: Theme.displayFont
        fontWeight: Font.ExtraBold
        fontSize: Math.round(Theme.fsHero * 1.35)
        progress: 0
        NumberAnimation on progress { from: 0; to: 1; duration: Theme.motionOn ? 2600 : 0; easing.type: Easing.InOutSine }
    }

    InkWave {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(60 * Theme.scale)
        running: hello.progress < 1 && Theme.motionOn
        level: running ? 0.35 + 0.35 * Math.abs(Math.sin(hello.progress * 19)) : 0
        color: Theme.accent
        sheen: Theme.sheen
        thickness: height * 0.42
        animated: !Theme.reducedMotion
    }

    Txt {
        Layout.fillWidth: true
        Layout.maximumWidth: Math.round(640 * Theme.scale)
        role: "lead"
        color: Theme.muted
        text: qsTr("Type or dictate, and Vocal Ink speaks for you: in calls, in games, on stream, or across the table. Setup takes about two minutes, and you can change everything later.")
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.s5
        Repeater {
            model: [
                { icon: "lock", text: qsTr("Works offline and privately") },
                { icon: "keyboard", text: qsTr("Fully usable with a keyboard") },
                { icon: "accessibility", text: qsTr("Screen reader and switch friendly") }
            ]
            Row {
                required property var modelData
                spacing: Theme.s2
                Icon { name: modelData.icon; color: Theme.accentText; size: Math.round(18 * Theme.scale); anchors.verticalCenter: parent.verticalCenter }
                Txt { text: modelData.text; role: "caption"; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
