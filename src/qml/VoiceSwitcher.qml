import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Quick switch: favourite voices, presets, speed and effect without leaving Talk.
Popup {
    id: pop
    signal browseAll()

    width: Math.round(380 * Math.min(1.35, Theme.scale))
    padding: Theme.s4
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        NumberAnimation { property: "scale"; from: Theme.reducedMotion ? 1 : 0.97; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.raised
        border.color: Theme.line
        border.width: Theme.hairline
    }

    readonly property var favorites: App.prefs["tts/favorites"] || []

    contentItem: ColumnLayout {
        spacing: Theme.s3

        Txt { text: qsTr("Favourite voices"); role: "caption"; font.weight: Font.DemiBold }
        Txt {
            visible: pop.favorites.length === 0
            text: App.shortcuts["voice.next"] ? qsTr("Star voices in Voices to switch between them here and with %1.").arg(App.nativeShortcut(App.shortcuts["voice.next"]))
                                              : qsTr("Star voices in Voices to switch between them here.")
            role: "caption"
            Layout.fillWidth: true
        }
        Repeater {
            model: pop.favorites
            delegate: AbstractButton {
                id: fav
                required property string modelData
                required property int index
                readonly property var info: App.voiceInfo(modelData)
                readonly property bool current: App.voiceKey === modelData
                Layout.fillWidth: true
                implicitHeight: Theme.control + 4
                hoverEnabled: true
                Accessible.name: info.name
                onClicked: { App.setVoice(modelData); pop.close() }
                contentItem: RowLayout {
                    spacing: Theme.s3
                    VoiceAvatar { name: fav.info.name; initials: fav.info.initials; local: fav.info.local; size: Math.round(30 * Theme.scale) }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Txt { text: fav.info.name; role: "label"; elide: Text.ElideRight; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                        Txt { text: fav.info.providerName + " · " + fav.info.language; role: "caption"; elide: Text.ElideRight; wrapMode: Text.NoWrap; Layout.fillWidth: true }
                    }
                    KeyCombo { visible: fav.index < 5; sequence: App.shortcuts["voice.fav" + (fav.index + 1)] || "" }
                    Icon { visible: fav.current; name: "check"; color: Theme.accentText; size: Math.round(18 * Theme.scale) }
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: fav.current ? Theme.accentWash : fav.hovered ? Theme.hover : "transparent"
                    FocusFrame { shown: fav.visualFocus; baseRadius: parent.radius }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line; visible: App.presets.count > 0 }
        Txt { visible: App.presets.count > 0; text: qsTr("Presets"); role: "caption"; font.weight: Font.DemiBold }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            visible: App.presets.count > 0
            Repeater {
                model: App.presets
                Chip {
                    required property string presetId
                    required property string name
                    text: name
                    iconName: "sliders-horizontal"
                    onClicked: { App.applyPreset(presetId); pop.close() }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Txt { text: qsTr("Speed"); role: "label"; Layout.preferredWidth: Math.round(64 * Theme.scale) }
            ValueSlider {
                Layout.fillWidth: true
                from: 50; to: 200; stepSize: 5
                label: qsTr("Speed")
                suffix: "%"
                value: App.prefs["tts/rate"]
                onMoved: App.prefs["tts/rate"] = value
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Txt { text: qsTr("Effect"); role: "label"; Layout.preferredWidth: Math.round(64 * Theme.scale) }
            Choice {
                Layout.fillWidth: true
                label: qsTr("Voice effect")
                model: App.effects
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(App.prefs["fx/effect"])
                onActivated: App.prefs["fx/effect"] = currentValue
            }
        }
        PillButton {
            Layout.fillWidth: true
            text: qsTr("Browse all voices")
            iconName: "audio-lines"
            onClicked: { pop.close(); pop.browseAll() }
        }
    }
}
