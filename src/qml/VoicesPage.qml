import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Every voice from every provider, how the current one is tuned, presets,
// and free offline voices to download.
Item {
    id: page
    property string tab: "browse"
    readonly property bool wide: width > Math.round(1060 * Theme.scale)

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
                Txt { text: qsTr("Voices"); role: "heading"; Accessible.role: Accessible.Heading }
                Txt {
                    text: qsTr("Pick how you sound. Local voices work offline and cost nothing; cloud voices use your own API keys.")
                    role: "lead"
                    color: Theme.muted
                    Layout.fillWidth: true
                }
            }
            Segmented {
                label: qsTr("Voices view")
                value: page.tab
                options: [
                    { value: "browse", label: qsTr("Browse"), icon: "search" },
                    { value: "tune", label: qsTr("Tune & presets"), icon: "sliders-horizontal" },
                    { value: "download", label: qsTr("Download"), icon: "download" }
                ]
                onActivated: (v) => page.tab = v
                Layout.alignment: Qt.AlignBottom
            }
        }

        // --- Browse ---------------------------------------------------------------------
        RowLayout {
            visible: page.tab === "browse"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s5

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.s3

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s2
                    Field {
                        id: search
                        Layout.fillWidth: true
                        iconName: "search"
                        placeholderText: qsTr("Search by name, accent, language, style…")
                        label: qsTr("Search voices")
                        onTextChanged: App.voices.search = text
                        Keys.onDownPressed: list.forceActiveFocus()
                    }
                    Choice {
                        label: qsTr("Language")
                        Layout.preferredWidth: Math.round(220 * Theme.scale)
                        model: [qsTr("All languages")].concat(App.voices.languages)
                        onActivated: App.voices.language = currentIndex === 0 ? "" : currentText
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.s2
                    Chip {
                        text: qsTr("All")
                        selected: App.voices.provider === "" && !App.voices.favoritesOnly
                        onClicked: { App.voices.provider = ""; App.voices.favoritesOnly = false }
                    }
                    Chip {
                        text: qsTr("Favourites")
                        iconName: "star"
                        selected: App.voices.favoritesOnly
                        onClicked: App.voices.favoritesOnly = !App.voices.favoritesOnly
                    }
                    Repeater {
                        model: App.voices.providers
                        Chip {
                            required property var modelData
                            text: modelData.name + (modelData.available ? "  " + modelData.count : "")
                            iconName: modelData.available ? (modelData.local ? "" : "globe") : "lock"
                            selected: App.voices.provider === modelData.id
                            tip: modelData.available ? (modelData.local ? qsTr("Runs on this computer") : qsTr("Cloud voices, uses your API key")) : modelData.reason
                            onClicked: App.voices.provider = (App.voices.provider === modelData.id ? "" : modelData.id)
                        }
                    }
                }

                // A provider that needs setup explains itself here.
                Repeater {
                    model: App.voices.providers
                    Card {
                        required property var modelData
                        visible: App.voices.provider === modelData.id && !modelData.available
                        Layout.fillWidth: true
                        title: qsTr("Set up %1").arg(modelData.name)
                        subtitle: modelData.reason
                        iconName: "lock"
                        ProviderKeyForm { providerId: modelData.id; Layout.fillWidth: true }
                    }
                }

                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.voices
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    highlightFollowsCurrentItem: true
                    currentIndex: -1
                    ScrollBar.vertical: InkScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Voices, %1 shown").arg(count)
                    Keys.onReturnPressed: if (currentItem) App.setVoice(currentItem.voiceKey)
                    Keys.onSpacePressed: if (currentItem) App.previewVoice(currentItem.voiceKey)

                    delegate: VoiceRow { width: ListView.view.width - 10 }

                    Column {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        spacing: Theme.s2
                        width: Math.min(parent.width, Math.round(380 * Theme.scale))
                        Icon { name: "search"; color: Theme.faint; size: Math.round(32 * Theme.scale); anchors.horizontalCenter: parent.horizontalCenter }
                        Txt {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: App.voices.favoritesOnly ? qsTr("No favourites yet. Star a voice to keep it here and switch to it with a shortcut.")
                                                           : qsTr("No voices match. Try fewer words, or another provider.")
                            color: Theme.muted
                        }
                    }
                }
            }

            // The voice you're using, always visible on wide windows.
            CurrentVoiceCard {
                visible: page.wide
                Layout.preferredWidth: Math.round(340 * Math.min(1.3, Theme.scale))
                Layout.alignment: Qt.AlignTop
            }
        }

        // --- Tune & presets ----------------------------------------------------------------
        Flickable {
            visible: page.tab === "tune"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: tuneCol.implicitHeight + Theme.s6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: InkScrollBar {}

            GridLayout {
                id: tuneCol
                width: parent.width - Theme.s2
                columns: page.wide ? 2 : 1
                columnSpacing: Theme.s5
                rowSpacing: Theme.s5

                CurrentVoiceCard { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; showTuning: true }
                PresetsCard { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop }
            }
        }

        // --- Download ---------------------------------------------------------------------------
        DownloadList {
            visible: page.tab === "download"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.piperVoices
            kind: "piper"
        }
    }
}
