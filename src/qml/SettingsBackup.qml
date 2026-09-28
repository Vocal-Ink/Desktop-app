import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    id: page
    signal runOnboarding()
    title: qsTr("Backup & updates")
    subtitle: qsTr("Move your setup to another computer, keep a copy, and stay up to date.")

    Card {
        Layout.fillWidth: true
        title: qsTr("Backup")
        subtitle: qsTr("One file with your settings, phrases, sounds, presets and learned words. API keys are never included.")
        iconName: "file-down"
        RowLayout {
            spacing: Theme.s2
            PillButton { kind: "primary"; iconName: "file-down"; text: qsTr("Save a backup…"); onClicked: saveDialog.open() }
            PillButton { iconName: "file-up"; text: qsTr("Restore from a backup…"); onClicked: openDialog.open() }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Updates")
        iconName: "rocket"
        Rectangle {
            visible: App.updateVersion !== ""
            Layout.fillWidth: true
            implicitHeight: upd.implicitHeight + Theme.s3 * 2
            radius: Theme.radius
            color: Theme.accentWash
            RowLayout {
                id: upd
                anchors.fill: parent
                anchors.margins: Theme.s3
                spacing: Theme.s3
                Icon { name: "sparkles"; color: Theme.accentText }
                Txt { Layout.fillWidth: true; text: qsTr("Vocal Ink %1 is available.").arg(App.updateVersion) }
                PillButton { kind: "primary"; text: qsTr("Get it"); iconName: "external-link"; onClicked: App.openUrl(App.updateUrl) }
            }
        }
        SettingRow {
            title: qsTr("Check for updates automatically")
            description: qsTr("Once at start-up. Only the version number is fetched from GitHub.")
            Toggle { tip: qsTr("Check for updates automatically"); checked: App.prefs["app/checkUpdates"] !== false; onToggled: App.prefs["app/checkUpdates"] = checked }
        }
        SettingRow {
            title: qsTr("You have version %1").arg(App.version)
            PillButton { small: true; iconName: "refresh-cw"; text: qsTr("Check now"); onClicked: App.checkForUpdates() }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Your data")
        iconName: "folder-open"
        SettingRow {
            title: qsTr("Data folder")
            description: App.dataFolder
            PillButton { small: true; iconName: "folder-open"; text: qsTr("Open"); onClicked: App.openDataFolder() }
        }
        SettingRow {
            title: qsTr("Run the setup again")
            description: qsTr("Walks through voice, virtual mic, dictation and shortcuts. Nothing is reset.")
            PillButton { small: true; iconName: "wand-sparkles"; text: qsTr("Start setup"); onClicked: page.runOnboarding() }
        }
        SettingRow {
            title: qsTr("Clear what I've said this session")
            PillButton { small: true; kind: "ghost"; iconName: "trash-2"; text: qsTr("Clear history"); onClicked: App.clearHistory() }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save a backup")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "vocalink"
        nameFilters: [qsTr("Vocal Ink backup (*.vocalink)")]
        onAccepted: {
            const err = App.exportSettings(selectedFile)
            App.notifyUser(err === "" ? qsTr("Backup saved.") : err, err === "" ? 0 : 2)
        }
    }
    FileDialog {
        id: openDialog
        title: qsTr("Restore from a backup")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Vocal Ink backup (*.vocalink)"), qsTr("All files (*)")]
        onAccepted: {
            const err = App.importSettings(selectedFile)
            App.notifyUser(err === "" ? qsTr("Backup restored.") : err, err === "" ? 0 : 2)
        }
    }
}
