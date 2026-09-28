import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    id: page
    signal runOnboarding()

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        Image {
            source: "qrc:/icons/app-256.png"
            sourceSize: Qt.size(160, 160)
            Layout.preferredWidth: Math.round(88 * Theme.scale)
            Layout.preferredHeight: Layout.preferredWidth
        }
        InkLine {
            Layout.fillWidth: true
            text: qsTr("Vocal Ink")
            fontFamily: Theme.displayFont
            fontWeight: Font.ExtraBold
            fontSize: Theme.fsHero
            progress: 0.62
        }
        Txt { text: qsTr("Version %1").arg(App.version); role: "caption"; font.family: Theme.monoFont }
        Txt {
            Layout.fillWidth: true
            Layout.maximumWidth: Math.round(620 * Theme.scale)
            role: "lead"
            color: Theme.muted
            text: qsTr("A voice for people who don't use their own: type or dictate, and Vocal Ink speaks for you, in calls, games, streams and in person. Free, open source and private by default.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            PillButton { iconName: "external-link"; text: qsTr("Website & source"); onClicked: App.openUrl("https://github.com/Vocal-Ink/Desktop-app") }
            PillButton { iconName: "flag"; text: qsTr("Report a problem"); onClicked: App.openUrl("https://github.com/Vocal-Ink/Desktop-app/issues/new") }
            PillButton { iconName: "book-open"; text: qsTr("Open-source licences"); onClicked: App.openUrl("https://github.com/Vocal-Ink/Desktop-app/blob/main/THIRD_PARTY_NOTICES.md") }
            PillButton { kind: "ghost"; iconName: "wand-sparkles"; text: qsTr("Run setup again"); onClicked: page.runOnboarding() }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Keys worth knowing")
        iconName: "keyboard"
        GridLayout {
            Layout.fillWidth: true
            columns: page.width > Math.round(700 * Theme.scale) ? 2 : 1
            columnSpacing: Theme.s6
            rowSpacing: Theme.s2
            Repeater {
                model: [
                    { keys: "Return", what: qsTr("Speak the message") },
                    { keys: "Shift+Return", what: qsTr("New line") },
                    { keys: "Esc", what: qsTr("Stop speaking") },
                    { keys: "Up", what: qsTr("Bring back an earlier message") },
                    { keys: "Tab", what: qsTr("Take the first suggestion") },
                    { keys: "Ctrl+K", what: qsTr("Search & commands") },
                    { keys: App.shortcuts["listen.ptt"] || "", what: qsTr("Hold to dictate (anywhere)") },
                    { keys: App.shortcuts["window.quickType"] || "", what: qsTr("Quick type over a game") },
                    { keys: App.shortcuts["panic.mute"] || "", what: qsTr("Stop everything and mute") },
                    { keys: "F11", what: qsTr("Show text full screen") }
                ]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.s3
                    Txt { text: modelData.what; Layout.fillWidth: true }
                    KeyCombo { sequence: modelData.keys; placeholder: qsTr("not set") }
                }
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Privacy")
        iconName: "shield-check"
        Txt {
            Layout.fillWidth: true
            color: Theme.muted
            text: qsTr("Everything runs on this computer unless you add a cloud provider's key. Then only the text you speak (or the audio you dictate, for cloud recognition) goes to that provider. Vocal Ink has no account, no analytics and no tracking.")
        }
    }
}
