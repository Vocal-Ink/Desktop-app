import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Ink.Core

// Phrases and sounds you fire with one tap or one key.
Item {
    id: page
    property string tab: "phrases"
    property string category: ""
    readonly property bool scanning: App.prefs["a11y/scanning"] === true && tab === "phrases"

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s6
        anchors.rightMargin: Theme.s6
        anchors.topMargin: Theme.s6
        spacing: Theme.s4

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s4
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Txt { text: qsTr("Board"); role: "heading"; Accessible.role: Accessible.Heading }
                Txt {
                    Layout.fillWidth: true
                    role: "lead"
                    color: Theme.muted
                    text: page.tab === "phrases" ? qsTr("Things you say often, one tap or one key away, even from inside a game.")
                                                 : qsTr("Short sounds played into your virtual mic: a laugh, a rimshot, your stream's jingle.")
                }
            }
            Segmented {
                Layout.alignment: Qt.AlignBottom
                label: qsTr("Board view")
                value: page.tab
                options: [
                    { value: "phrases", label: qsTr("Phrases"), icon: "message-square-text" },
                    { value: "sounds", label: qsTr("Sounds"), icon: "music" }
                ]
                onActivated: (v) => page.tab = v
            }
        }

        // --- Phrases ---------------------------------------------------------------------------
        RowLayout {
            visible: page.tab === "phrases"
            Layout.fillWidth: true
            spacing: Theme.s2
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s2
                Repeater {
                    model: [""].concat(App.phrases.categories)
                    Chip {
                        required property string modelData
                        text: modelData === "" ? qsTr("All") : modelData
                        selected: page.category === modelData
                        onClicked: page.category = modelData
                    }
                }
            }
            PillButton { kind: "primary"; iconName: "plus"; text: qsTr("New phrase"); onClicked: editor.openFor(-1) }
            PillButton {
                kind: "ghost"
                iconName: "ellipsis"
                tip: qsTr("More")
                onClicked: moreMenu.open()
                Menu {
                    id: moreMenu
                    y: parent.height
                    MenuItem { text: qsTr("Restore the default phrases"); onTriggered: resetSheet.open() }
                }
            }
        }

        GridView {
            id: grid
            visible: page.tab === "phrases"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: RoleFilter { id: filtered; sourceModel: App.phrases; role: "category"; value: page.category }
            readonly property int columns: Math.max(1, Math.floor(width / Math.round(236 * Math.min(1.5, Theme.scale))))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round(112 * Math.min(1.6, Theme.scale))
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            currentIndex: -1
            ScrollBar.vertical: InkScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Phrases")

            delegate: PhraseTile {
                width: grid.cellWidth
                height: grid.cellHeight
                scanned: page.scanning && scanTimer.index === index
                onEdit: editor.openFor(phraseIndex)
            }

            Column {
                anchors.centerIn: parent
                visible: grid.count === 0
                spacing: Theme.s3
                Txt { text: qsTr("No phrases yet"); role: "title"; anchors.horizontalCenter: parent.horizontalCenter }
                PillButton { kind: "primary"; iconName: "plus"; text: qsTr("Add your first phrase"); anchors.horizontalCenter: parent.horizontalCenter; onClicked: editor.openFor(-1) }
            }
        }

        // Switch access: the highlight walks the tiles; any key or click picks one.
        Timer {
            id: scanTimer
            property int index: -1
            running: page.scanning && page.visible && grid.count > 0
            repeat: true
            interval: Math.max(400, App.prefs["a11y/scanIntervalMs"] || 1200)
            onTriggered: index = (index + 1) % grid.count
        }
        Keys.enabled: page.scanning
        Keys.onPressed: (event) => {
            if (scanTimer.index >= 0) {
                App.trigger("phrase:" + filtered.sourceRow(scanTimer.index))
                event.accepted = true
            }
        }

        // --- Sounds ----------------------------------------------------------------------------
        RowLayout {
            visible: page.tab === "sounds"
            Layout.fillWidth: true
            spacing: Theme.s2
            Txt {
                Layout.fillWidth: true
                role: "caption"
                text: qsTr("Sounds play into your virtual mic (and your headphones if “Hear my voice too” is on). Give them shortcuts to fire them from anywhere.")
            }
            PillButton { kind: "primary"; iconName: "plus"; text: qsTr("Add sounds"); onClicked: fileDialog.open() }
            PillButton { kind: "danger"; iconName: "square"; text: qsTr("Stop all"); onClicked: App.sounds.stopAll() }
        }

        GridView {
            id: pads
            visible: page.tab === "sounds"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.sounds
            readonly property int columns: Math.max(2, Math.floor(width / Math.round(180 * Math.min(1.5, Theme.scale))))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round(cellWidth * 0.78)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: InkScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Sounds")
            delegate: SoundPad {
                width: pads.cellWidth
                height: pads.cellHeight
                onEdit: soundEditor.openFor(soundId, name, tint, hotkey, gain)
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: pads.count === 0
                spacing: Theme.s3
                width: Math.min(parent.width, Math.round(420 * Theme.scale))
                Icon { name: "music"; size: Math.round(40 * Theme.scale); color: Theme.faint; Layout.alignment: Qt.AlignHCenter }
                Txt { text: qsTr("Your soundboard is empty"); role: "title"; Layout.alignment: Qt.AlignHCenter }
                Txt {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.muted
                    text: qsTr("Add WAV, MP3, OGG or FLAC files. Vocal Ink keeps its own copy, so moving the originals won't break anything.")
                }
                PillButton { kind: "primary"; iconName: "plus"; text: qsTr("Add sounds"); Layout.alignment: Qt.AlignHCenter; onClicked: fileDialog.open() }
            }

            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    for (let i = 0; i < drop.urls.length; ++i) {
                        const err = App.sounds.addFile(drop.urls[i])
                        if (err !== "") App.notifyUser(err, 1)
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Add sounds")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Audio files (*.wav *.mp3 *.ogg *.flac *.m4a)"), qsTr("All files (*)")]
        onAccepted: {
            for (let i = 0; i < selectedFiles.length; ++i) {
                const err = App.sounds.addFile(selectedFiles[i])
                if (err !== "") App.notifyUser(err, 1)
            }
        }
    }

    PhraseEditor { id: editor }
    SoundEditor { id: soundEditor }

    Sheet {
        id: resetSheet
        title: qsTr("Restore the default phrases?")
        message: qsTr("Your own phrases will be replaced by the starter set.")
        iconName: "rotate-ccw"
        footer: [
            PillButton { text: qsTr("Restore defaults"); kind: "danger"; onClicked: { App.phrases.resetToDefaults(); resetSheet.close() } },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: resetSheet.close() }
        ]
    }
}
