import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Offline models to download: Piper voices or Whisper speech models.
ColumnLayout {
    id: root
    property var model
    property string kind: "piper" // piper | whisper

    spacing: Theme.s3

    Card {
        visible: root.kind === "piper" && !root.model.runtimeInstalled
        Layout.fillWidth: true
        title: qsTr("Natural offline voices need the Piper engine")
        subtitle: qsTr("A one-time download of about 25 MB. Everything then runs on this computer.")
        iconName: "download"
        RowLayout {
            spacing: Theme.s3
            PillButton {
                kind: "primary"
                text: root.model.runtimeDownloading ? qsTr("Downloading…") : qsTr("Download Piper")
                enabled: !root.model.runtimeDownloading
                iconName: "download"
                onClicked: root.model.downloadRuntime()
            }
            LevelBar {
                visible: root.model.runtimeDownloading
                Layout.fillWidth: true
                level: root.model.runtimeProgress
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Field {
            Layout.fillWidth: true
            iconName: "search"
            label: qsTr("Filter")
            placeholderText: root.kind === "piper" ? qsTr("Filter by language or name, e.g. “en_GB” or “amy”") : qsTr("Filter models")
            onTextChanged: root.model.filter = text
        }
        PillButton {
            text: qsTr("Get the recommended one")
            iconName: "sparkles"
            onClicked: root.model.downloadRecommended()
        }
    }

    Txt {
        visible: root.model.status !== ""
        text: root.model.status
        role: "caption"
        Layout.fillWidth: true
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: Theme.s2
        model: root.model
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: InkScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: root.kind === "piper" ? qsTr("Downloadable voices") : qsTr("Speech models")

        delegate: Rectangle {
            id: item
            required property string name
            required property string title
            required property string details
            required property string size
            required property bool recommended
            required property bool installed
            required property bool downloading
            required property double progress
            required property bool inUse
            width: ListView.view.width - 10
            implicitHeight: row.implicitHeight + Theme.s3 * 2
            radius: Theme.radius
            color: item.inUse ? Theme.accentWash : Theme.surface
            border.color: item.inUse ? Theme.alpha(Theme.accent, 0.4) : Theme.line
            border.width: Theme.hairline
            Accessible.role: Accessible.ListItem
            Accessible.name: title + (installed ? qsTr(", downloaded") : "") + (recommended ? qsTr(", recommended") : "")

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: Theme.s3
                anchors.leftMargin: Theme.s4
                spacing: Theme.s3
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: Theme.s2
                        Txt { text: item.title; role: "label" }
                        Rectangle {
                            visible: item.recommended
                            implicitWidth: rec.implicitWidth + Theme.s3
                            implicitHeight: rec.implicitHeight + 4
                            radius: height / 2
                            color: Theme.okWash
                            Text { id: rec; anchors.centerIn: parent; text: qsTr("Recommended"); font.family: Theme.uiFont; font.pixelSize: Theme.fsXs; font.weight: Font.Bold; color: Theme.ok }
                        }
                    }
                    Txt { text: [item.details, item.size].filter(s => s).join(" · "); role: "caption"; Layout.fillWidth: true }
                    LevelBar { visible: item.downloading; Layout.fillWidth: true; level: item.progress; Layout.topMargin: Theme.s1 }
                }
                PillButton {
                    visible: root.kind === "whisper" && item.installed
                    small: true
                    kind: item.inUse ? "ghost" : "secondary"
                    enabled: !item.inUse
                    text: item.inUse ? qsTr("In use") : qsTr("Use")
                    iconName: item.inUse ? "check" : ""
                    onClicked: root.model.use(item.name)
                }
                PillButton {
                    visible: !item.installed
                    small: true
                    kind: item.downloading ? "ghost" : "primary"
                    text: item.downloading ? qsTr("Cancel") : qsTr("Download")
                    iconName: item.downloading ? "x" : "download"
                    onClicked: item.downloading ? root.model.cancel(item.name) : root.model.download(item.name)
                }
                PillButton {
                    visible: root.kind === "piper" && item.installed
                    small: true
                    text: qsTr("Preview")
                    iconName: "play"
                    onClicked: App.previewVoice("piper:" + item.name)
                }
                IconButton {
                    visible: item.installed
                    small: true
                    iconName: "trash-2"
                    tip: qsTr("Delete “%1” from this computer").arg(item.title)
                    onClicked: root.model.remove(item.name)
                }
            }
        }
    }
}
