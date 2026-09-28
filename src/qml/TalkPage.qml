import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// The main screen: what you're saying, what you've said, and where it's going.
Item {
    id: page
    signal navigate(string page)
    property alias composer: composer

    function focusComposer() { composer.focusEditor() }

    Connections {
        target: App
        function onTranscriptReady(text) { composer.insert(text) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Header: voice, routing, real mic -----------------------------------------
        Item {
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight + Theme.s4 * 2

            RowLayout {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.s6
                anchors.rightMargin: Theme.s4
                spacing: Theme.s3

                AbstractButton {
                    id: voicePill
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    implicitHeight: Math.round(Theme.control * 1.15)
                    implicitWidth: pillRow.implicitWidth + Theme.s3 * 2
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Voice: %1. Change voice").arg(App.voiceName)
                    onClicked: switcher.opened ? switcher.close() : switcher.open()
                    contentItem: Item {
                        RowLayout {
                            id: pillRow
                            anchors.verticalCenter: parent.verticalCenter
                            x: Theme.s3 - voicePill.leftPadding
                            spacing: Theme.s3
                            VoiceAvatar {
                                name: App.voiceName
                                initials: App.voiceInfo(App.voiceKey).initials || ""
                                local: App.voiceInfo(App.voiceKey).local || false
                                speaking: App.speaking
                            }
                            ColumnLayout {
                                spacing: 0
                                Txt {
                                    text: App.voiceName !== "" ? App.voiceName : qsTr("Pick a voice")
                                    role: "label"
                                    font.pixelSize: Theme.fsLg
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: Math.round(260 * Theme.scale)
                                }
                                Txt {
                                    text: App.voiceProviderName !== "" ? App.voiceProviderName + (App.voiceLanguage ? " · " + App.voiceLanguage : "") : qsTr("No voice selected")
                                    role: "caption"
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: Math.round(260 * Theme.scale)
                                }
                            }
                            Icon { name: "chevron-down"; color: Theme.muted; size: Math.round(16 * Theme.scale) }
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusLg
                        color: voicePill.down ? Theme.pressed : voicePill.hovered || switcher.opened ? Theme.hover : "transparent"
                        FocusFrame { shown: voicePill.visualFocus; baseRadius: parent.radius }
                    }
                    VoiceSwitcher {
                        id: switcher
                        y: voicePill.height + 6
                        onBrowseAll: page.navigate("voices")
                    }
                }

                Item { Layout.fillWidth: true }

                StatusChip {
                    visible: App.listening || App.transcribing
                    tone: "accent"
                    pulse: App.listening
                    text: App.transcribing ? qsTr("Transcribing") : qsTr("Listening")
                    onClicked: App.cancelListening()
                    tip: qsTr("Click to stop listening")
                }
                StatusChip {
                    readonly property string mode: App.prefs["mic/mode"] || "off"
                    visible: mode !== "off"
                    tone: App.micLive ? "live" : "idle"
                    pulse: App.micLive
                    iconName: App.micLive ? "mic" : "mic-off"
                    text: App.micLive ? qsTr("Real mic LIVE") : qsTr("Real mic muted")
                    tip: App.micLive ? qsTr("People can hear your real microphone. Click to mute.")
                                     : qsTr("Your real microphone is not being sent.")
                    onClicked: App.setMicLive(!App.micLive)
                }
                StatusChip {
                    tone: App.routeState === "virtual" ? "ok" : App.routeState === "missing" ? "live" : "warn"
                    iconName: App.routeState === "virtual" ? "cable" : App.routeState === "missing" ? "circle-alert" : "volume-2"
                    text: App.routeState === "virtual" ? qsTr("Into %1").arg(App.routeName)
                        : App.routeState === "missing" ? qsTr("Output unplugged")
                        : qsTr("Speakers only")
                    tip: App.routeState === "virtual" ? qsTr("Apps that use this virtual mic hear your voice.")
                       : App.routeState === "missing" ? qsTr("The output device isn't connected. Pick another in Audio & mic.")
                       : qsTr("Only people in the room hear you. Set up the virtual mic so calls and games can too.")
                    onClicked: page.navigate("audio")
                }
                PillButton {
                    visible: App.speaking
                    kind: "danger"
                    small: true
                    text: qsTr("Stop")
                    iconName: "square"
                    shortcut: "Esc"
                    onClicked: App.stop()
                }
                IconButton {
                    iconName: "fullscreen"
                    tip: qsTr("Show text to someone (%1)").arg(App.shortcutFor("window.showText") || "F11")
                    onClicked: App.trigger("window.showText")
                }
            }
        }

        // --- Stage: the current line, and the ink drying above it ---------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: thread
                anchors.fill: parent
                anchors.leftMargin: Theme.s6
                anchors.rightMargin: Theme.s6
                model: App.history
                verticalLayoutDirection: ListView.BottomToTop
                spacing: Theme.s4
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: InkScrollBar {}
                visible: count > 0
                Accessible.role: Accessible.List
                Accessible.name: qsTr("What you've said")


                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.slow; easing.type: Easing.OutCubic }
                }
                header: Item { height: Theme.s5; width: 1 }
                footer: Item { height: Theme.s8; width: 1 }

                delegate: Item {
                    id: line
                    required property int index
                    required property string text
                    required property string voiceName
                    required property string timeText
                    required property int status
                    required property string error
                    required property double messageId
                    readonly property bool latest: index === 0
                    readonly property bool isCurrent: messageId === App.currentId
                    width: ListView.view.width
                    height: body.implicitHeight
                    opacity: latest ? 1 : Math.max(0.42, 0.9 - index * 0.1)

                    HoverHandler { id: hover }

                    ColumnLayout {
                        id: body
                        width: parent.width
                        spacing: Theme.s1

                        RowLayout {
                            spacing: Theme.s2
                            Layout.fillWidth: true
                            Txt {
                                text: line.timeText + (line.voiceName ? " · " + line.voiceName : "")
                                role: "caption"
                                color: Theme.faint
                                wrapMode: Text.NoWrap
                            }
                            Txt {
                                visible: line.status === HistoryModel.Queued || line.status === HistoryModel.Failed || line.status === HistoryModel.Stopped
                                text: line.status === HistoryModel.Queued ? qsTr("waiting")
                                    : line.status === HistoryModel.Failed ? qsTr("couldn't speak")
                                    : qsTr("stopped")
                                role: "caption"
                                color: line.status === HistoryModel.Failed ? Theme.live : Theme.faint
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Row {
                                spacing: 2
                                opacity: hover.hovered || actionsFocus.activeFocus ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                                FocusScope { id: actionsFocus; width: 0; height: 0 }
                                IconButton { small: true; iconName: "repeat"; tip: qsTr("Say again"); onClicked: App.speak(line.text) }
                                IconButton { small: true; iconName: "copy"; tip: qsTr("Copy"); onClicked: App.copy(line.text) }
                                IconButton {
                                    small: true
                                    iconName: "star"
                                    tip: qsTr("Save as a quick phrase")
                                    onClicked: { App.phrases.add(line.text, qsTr("Saved"), "", "", ""); App.notifyUser(qsTr("Saved to your board."), 0) }
                                }
                            }
                        }

                        // Widths are bound up front so each line knows its height
                        // before the list positions it.
                        Loader {
                            Layout.preferredWidth: line.width
                            Layout.preferredHeight: item ? item.implicitHeight : 0
                            sourceComponent: line.latest ? inkLine : dryLine
                        }
                        Component {
                            id: inkLine
                            InkLine {
                                width: line.width
                                text: line.text
                                progress: line.isCurrent ? App.lineProgress : (line.status === HistoryModel.Queued ? 0 : 1)
                                animate: line.isCurrent || line.status === HistoryModel.Queued
                            }
                        }
                        Component {
                            id: dryLine
                            Txt {
                                width: line.width
                                text: line.text
                                font.pixelSize: Math.round(Theme.fsXl * Math.max(0.8, (App.prefs["ui/stageScale"] || 100) / 100))
                                font.family: Theme.stageFont
                                font.weight: Font.DemiBold
                                color: line.status === HistoryModel.Failed ? Theme.live : line.status === HistoryModel.Queued ? Theme.inkUnwritten : Theme.muted
                            }
                        }
                        Txt {
                            visible: line.error !== "" && line.latest
                            text: line.error
                            role: "caption"
                            color: Theme.live
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Nothing said yet: show how it works instead of an empty page.
            ColumnLayout {
                visible: thread.count === 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: Math.round(parent.height * 0.12)
                anchors.leftMargin: Theme.s8
                anchors.rightMargin: Theme.s6
                spacing: Theme.s4

                InkLine {
                    Layout.fillWidth: true
                    text: qsTr("Say it your way.")
                    fontFamily: Theme.displayFont
                    fontWeight: Font.ExtraBold
                    fontSize: Math.round(Theme.fsHero * 1.2)
                    progress: 0
                    animate: true
                    NumberAnimation on progress {
                        from: 0; to: 1
                        duration: Theme.motionOn ? 1800 : 0
                        easing.type: Easing.InOutSine
                    }
                }
                Txt {
                    text: qsTr("Type below and press Enter. Your words fill with ink as Vocal Ink says them, in your voice, into your calls, games and streams.")
                    role: "lead"
                    color: Theme.muted
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.round(620 * Theme.scale)
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.s5
                    Repeater {
                        model: [
                            { keys: "Return", label: qsTr("speak") },
                            { keys: App.prefs["keybinds/listen.ptt"] || "Ctrl+Alt+Space", label: qsTr("hold to dictate") },
                            { keys: "Ctrl+K", label: qsTr("find anything") },
                            { keys: App.prefs["keybinds/window.quickType"] || "Ctrl+Alt+T", label: qsTr("type over a game") }
                        ]
                        Row {
                            required property var modelData
                            spacing: Theme.s2
                            KeyCombo { sequence: modelData.keys; anchors.verticalCenter: parent.verticalCenter }
                            Txt { text: modelData.label; role: "caption"; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
        }

        // --- Composer dock ------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.bottomMargin: Theme.s4
            spacing: Theme.s3

            Composer {
                id: composer
                Layout.fillWidth: true
            }
            PhraseTray {
                Layout.fillWidth: true
                visible: App.prefs["ui/showPhraseTray"] !== false
                onEdit: page.navigate("board")
            }
        }
    }
}
