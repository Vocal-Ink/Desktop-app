import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    id: step
    title: qsTr("Let other apps hear you")
    lead: qsTr("Vocal Ink speaks into a virtual microphone. In Discord, Zoom or your game, choose that microphone and they hear your voice.")

    readonly property var vm: App.virtualMic

    RouteDiagram { Layout.fillWidth: true; Layout.topMargin: Theme.s2 }

    Card {
        Layout.fillWidth: true
        title: vm.state === VirtualDriver.Installed ? qsTr("Virtual mic ready") : qsTr("Install the virtual mic")
        subtitle: vm.statusText
        iconName: "cable"
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            PillButton {
                visible: step.vm.state === VirtualDriver.NotInstalled || step.vm.state === VirtualDriver.Failed
                kind: "primary"
                iconName: "download"
                text: step.vm.busy ? qsTr("Installing…") : qsTr("Install")
                enabled: !step.vm.busy
                onClicked: step.vm.install()
            }
            PillButton {
                visible: App.routeState !== "virtual" && App.suggestedOutput() !== ""
                kind: step.vm.state === VirtualDriver.Installed ? "primary" : "secondary"
                iconName: "cable"
                text: qsTr("Send my voice into it")
                onClicked: App.useDevice("output", App.suggestedOutput())
            }
            PillButton {
                visible: App.platform !== "linux" && step.vm.state !== VirtualDriver.Installed
                iconName: "external-link"
                text: App.platform === "windows" ? qsTr("Use VB-CABLE instead") : qsTr("Use BlackHole instead")
                onClicked: App.openUrl(App.platform === "windows" ? "https://vb-audio.com/Cable/" : "https://existential.audio/blackhole/")
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Voice output")
        subtitle: qsTr("Pick the virtual mic's input side here. “System default” only plays on your speakers.")
        iconName: "volume-2"
        DeviceChoice { kind: "output"; model: App.outputs; Layout.fillWidth: true }
        RowLayout {
            spacing: Theme.s3
            PillButton { iconName: "ear"; text: App.routingCheckRunning ? qsTr("Listening…") : qsTr("Check that apps can hear me"); enabled: !App.routingCheckRunning; onClicked: App.startRoutingCheck() }
            Txt { id: result; Layout.fillWidth: true; role: "caption" }
        }
        Connections {
            target: App
            function onRoutingFinished(heard, detail) { result.text = detail; result.color = heard ? Theme.ok : Theme.warn }
        }
    }

    Txt {
        Layout.fillWidth: true
        role: "caption"
        text: qsTr("Then, in the other app: Settings → Voice → Input device → “%1”.").arg(vm.inputDeviceName)
    }
}
