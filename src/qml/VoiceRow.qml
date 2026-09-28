import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// One voice in the browser: preview, star, use.
ItemDelegate {
    id: row
    required property int index
    required property string key
    required property string name
    required property string provider
    required property string languageLabel
    required property string gender
    required property string description
    required property bool favorite
    required property bool local
    required property string initials
    readonly property string voiceKey: key
    readonly property bool inUse: App.voiceKey === key
    readonly property bool previewingThis: App.previewing && App.previewKey === key

    implicitHeight: Math.round(Theme.control * 1.6)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    highlighted: ListView.isCurrentItem
    padding: Theme.s3
    Accessible.role: Accessible.ListItem
    Accessible.name: name + ", " + provider + ", " + languageLabel + (inUse ? qsTr(", in use") : "")
    onClicked: { ListView.view.currentIndex = index; App.previewVoice(key) }
    onDoubleClicked: App.setVoice(key)

    contentItem: RowLayout {
        spacing: Theme.s3
        VoiceAvatar {
            name: row.name
            initials: row.initials
            local: row.local
            speaking: row.previewingThis
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                spacing: Theme.s2
                Layout.fillWidth: true
                Txt { text: row.name; role: "label"; wrapMode: Text.NoWrap; elide: Text.ElideRight; Layout.fillWidth: true; Layout.maximumWidth: implicitWidth + 1 }
                Rectangle {
                    visible: row.inUse
                    implicitWidth: inUseText.implicitWidth + Theme.s3
                    implicitHeight: inUseText.implicitHeight + 4
                    radius: height / 2
                    color: Theme.accentWash
                    Text { id: inUseText; anchors.centerIn: parent; text: qsTr("In use"); font.family: Theme.uiFont; font.pixelSize: Theme.fsXs; font.weight: Font.Bold; color: Theme.accentText }
                }
                Item { Layout.fillWidth: true }
            }
            Txt {
                Layout.fillWidth: true
                role: "caption"
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                text: [row.provider, row.languageLabel, row.gender, row.description].filter(s => s && s.length).join(" · ")
            }
        }
        IconButton {
            iconName: row.previewingThis ? "square" : "play"
            tip: row.previewingThis ? qsTr("Stop preview") : qsTr("Preview (plays only to you)")
            active: row.previewingThis
            onClicked: row.previewingThis ? App.stopPreview() : App.previewVoice(row.key)
        }
        IconButton {
            iconName: "star"
            tip: row.favorite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
            tint: row.favorite ? Theme.warn : Theme.muted
            onClicked: App.voices.toggleFavorite(row.key)
            Icon {
                anchors.centerIn: parent
                visible: row.favorite
                name: "star"
                filled: true
                color: Theme.warn
                size: Math.round(18 * Theme.scale)
            }
        }
        PillButton {
            small: true
            kind: row.inUse ? "ghost" : "secondary"
            enabled: !row.inUse
            text: row.inUse ? qsTr("Using") : qsTr("Use")
            iconName: row.inUse ? "check" : ""
            onClicked: App.setVoice(row.key)
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: row.inUse ? Theme.accentWash : row.highlighted ? Theme.hover : row.hovered ? Theme.hover : "transparent"
        border.color: row.inUse ? Theme.alpha(Theme.accent, 0.35) : "transparent"
        border.width: Theme.hairline
        FocusFrame { shown: row.visualFocus; baseRadius: parent.radius }
    }
}
