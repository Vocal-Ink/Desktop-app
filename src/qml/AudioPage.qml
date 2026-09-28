import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Where the voice goes, what you hear, and your real microphone.
ScrollPage {
    id: page
    signal navigate(string page)

    title: qsTr("Audio & mic")
    subtitle: qsTr("Vocal Ink speaks into a virtual microphone. Pick that microphone in Discord, Zoom, games or OBS and they hear your voice.")

    readonly property var vm: App.virtualMic
    readonly property bool driverReady: vm.state === VirtualDriver.Installed
    readonly property bool driverOffered: vm.state === VirtualDriver.NotInstalled || vm.state === VirtualDriver.Installed
                                           || vm.state === VirtualDriver.RestartNeeded || vm.state === VirtualDriver.Failed

    // --- Virtual microphone ---------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Virtual microphone")
        iconName: "cable"

        // How the sound travels, as a picture.
        RouteDiagram { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 10
                implicitHeight: 10
                Layout.topMargin: 6
                radius: 5
                color: page.driverReady || App.routeState === "virtual" ? Theme.ok
                     : page.vm.state === VirtualDriver.RestartNeeded ? Theme.warn
                     : page.vm.state === VirtualDriver.Failed ? Theme.live : Theme.faint
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Txt {
                    Layout.fillWidth: true
                    role: "label"
                    text: page.driverReady ? qsTr("%1 is installed").arg(page.vm.displayName)
                        : page.vm.state === VirtualDriver.RestartNeeded ? qsTr("Restart to finish installing")
                        : page.vm.state === VirtualDriver.NotInstalled ? qsTr("Install the Vocal Ink virtual microphone")
                        : App.routeState === "virtual" ? qsTr("Using %1").arg(App.routeName)
                        : qsTr("You need a virtual audio cable")
                }
                Txt { Layout.fillWidth: true; role: "caption"; text: page.vm.statusText; visible: text !== "" }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            PillButton {
                visible: page.vm.state === VirtualDriver.NotInstalled || page.vm.state === VirtualDriver.Failed
                kind: "primary"
                iconName: "download"
                text: page.vm.busy ? qsTr("Installing…") : qsTr("Install virtual mic")
                enabled: !page.vm.busy
                tip: page.vm.needsAdmin ? qsTr("Your computer will ask for your password once.") : ""
                onClicked: page.vm.install()
            }
            PillButton {
                visible: page.driverReady && App.routeState !== "virtual"
                kind: "primary"
                iconName: "cable"
                text: qsTr("Send my voice into it")
                onClicked: App.useDevice("output", App.suggestedOutput())
            }
            PillButton {
                text: App.routingCheckRunning ? qsTr("Listening for the test tone…") : qsTr("Check that apps can hear me")
                iconName: "ear"
                enabled: !App.routingCheckRunning
                onClicked: App.startRoutingCheck()
            }
            PillButton {
                visible: page.driverReady
                kind: "ghost"
                iconName: "trash-2"
                text: qsTr("Uninstall")
                enabled: !page.vm.busy
                onClicked: page.vm.uninstall()
            }
        }

        LevelBar {
            id: routeLevel
            visible: App.routingCheckRunning
            Layout.fillWidth: true
            tint: Theme.ok
        }
        Rectangle {
            id: routeResult
            property bool heard: false
            property string detail: ""
            visible: detail !== ""
            Layout.fillWidth: true
            implicitHeight: resultRow.implicitHeight + Theme.s3 * 2
            radius: Theme.radius
            color: heard ? Theme.okWash : Theme.warnWash
            RowLayout {
                id: resultRow
                anchors.fill: parent
                anchors.margins: Theme.s3
                spacing: Theme.s3
                Icon { name: routeResult.heard ? "circle-check" : "triangle-alert"; color: routeResult.heard ? Theme.ok : Theme.warn; Layout.alignment: Qt.AlignTop }
                Txt { Layout.fillWidth: true; text: routeResult.detail; font.pixelSize: Theme.fsSm }
            }
        }
        Connections {
            target: App
            function onRoutingProgress(level) { routeLevel.level = level }
            function onRoutingFinished(heard, detail) { routeResult.heard = heard; routeResult.detail = detail }
        }

        // Other cables still work, and are the fallback where our driver isn't available.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            visible: !page.driverReady
            Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
            Txt {
                Layout.fillWidth: true
                role: "caption"
                text: App.platform === "windows" ? qsTr("Prefer VB-CABLE? It's free and works the same way: install it, then pick “CABLE Input” below.")
                    : App.platform === "macos" ? qsTr("Prefer BlackHole? It's free and works the same way: install BlackHole 2ch, then pick it below.")
                    : qsTr("Already have a virtual sink? Pick it as the voice output below.")
            }
            RowLayout {
                spacing: Theme.s2
                PillButton {
                    visible: App.platform !== "linux"
                    small: true
                    iconName: "external-link"
                    text: App.platform === "windows" ? qsTr("Get VB-CABLE") : qsTr("Get BlackHole")
                    onClicked: App.openUrl(App.platform === "windows" ? "https://vb-audio.com/Cable/" : "https://existential.audio/blackhole/")
                }
                PillButton {
                    small: true
                    kind: "ghost"
                    iconName: "search"
                    text: qsTr("Find my cable")
                    onClicked: {
                        const id = App.suggestedOutput()
                        if (id !== "") { App.useDevice("output", id); App.notifyUser(qsTr("Found it. Your voice now goes to %1.").arg(App.routeName), 0) }
                        else App.notifyUser(qsTr("No virtual cable found. Install one, then try again."), 1)
                    }
                }
            }
        }
    }

    // --- Output ------------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Voice output")
        subtitle: qsTr("Where Vocal Ink plays your voice, and whether you hear it too.")
        iconName: "volume-2"

        SettingRow {
            title: qsTr("Play my voice into")
            description: qsTr("Choose the virtual mic's input side (“Vocal Ink Voice”, “CABLE Input”, “BlackHole”).")
            DeviceChoice { kind: "output"; model: App.outputs }
        }
        SettingRow {
            title: qsTr("Voice volume")
            ValueSlider {
                label: qsTr("Voice volume")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["audio/outputVolume"]
                onMoved: App.prefs["audio/outputVolume"] = value
            }
        }
        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
        SettingRow {
            title: qsTr("Hear my voice too")
            description: qsTr("Also play it on your headphones, so you know what others hear.")
            Toggle {
                tip: qsTr("Hear my voice too")
                checked: App.prefs["audio/monitorEnabled"] === true
                onToggled: App.prefs["audio/monitorEnabled"] = checked
            }
        }
        SettingRow {
            visible: App.prefs["audio/monitorEnabled"] === true
            title: qsTr("My headphones")
            DeviceChoice { kind: "monitor"; model: App.outputs }
        }
        SettingRow {
            visible: App.prefs["audio/monitorEnabled"] === true
            title: qsTr("Headphone volume")
            ValueSlider {
                label: qsTr("Headphone volume")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["audio/monitorVolume"]
                onMoved: App.prefs["audio/monitorVolume"] = value
            }
        }
    }

    // --- Real microphone ------------------------------------------------------------------
    RealMicCard { Layout.fillWidth: true }

    Card {
        Layout.fillWidth: true
        title: qsTr("Dictation microphone")
        subtitle: qsTr("The mic Vocal Ink listens to when you dictate. Set models and languages in Settings → Speech input.")
        iconName: "mic-vocal"
        SettingRow {
            title: qsTr("Listen to")
            DeviceChoice { kind: "input"; model: App.inputs }
        }
        RowLayout {
            spacing: Theme.s2
            PillButton { small: true; kind: "ghost"; text: qsTr("Speech input settings"); iconName: "arrow-right"; onClicked: page.navigate("settings:speech") }
        }
    }
}
