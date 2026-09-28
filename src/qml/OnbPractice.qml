import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    id: step
    title: said ? qsTr("That's your voice now.") : qsTr("Say your first words")
    lead: said ? (App.shortcuts["window.palette"] ? qsTr("Everything you need is on the Talk page. %1 finds anything else.").arg(App.nativeShortcut(App.shortcuts["window.palette"]))
                                                   : qsTr("Everything you need is on the Talk page."))
               : qsTr("Type anything and press Enter. Watch it fill with ink as it's spoken.")
    property bool said: false

    Connections {
        target: App
        function onSpoken() { step.said = true }
    }

    InkLine {
        Layout.fillWidth: true
        Layout.minimumHeight: Theme.stageSize * 1.4
        text: App.currentLine !== "" ? App.currentLine : qsTr("Hi everyone, it's good to be here.")
        progress: App.currentLine !== "" ? App.lineProgress : 0
        animate: true
    }
    Composer {
        id: composer
        Layout.fillWidth: true
        Component.onCompleted: {
            composer.text = qsTr("Hi everyone, it's good to be here.")
            composer.focusEditor()
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: Theme.s2
        Repeater {
            model: [qsTr("Hi! Give me a second to type."), qsTr("Can you hear me okay?"), qsTr("Thank you!")]
            Chip { required property string modelData; text: modelData; onClicked: App.speak(modelData) }
        }
    }
}
