import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// First-run setup, full window. Every step can be skipped and revisited later
// from Settings; nothing here is required to start talking.
Rectangle {
    id: onb
    property int step: 0
    signal finished()

    color: Theme.bg
    focus: true
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Vocal Ink setup")

    readonly property var uses: App.prefs["ui/uses"] || []
    function uses_(u) { return uses.indexOf(u) >= 0 }

    // The route adapts to what you'll use Vocal Ink for.
    readonly property var steps: {
        const s = [
            { id: "welcome", name: qsTr("Welcome") },
            { id: "uses", name: qsTr("Your day") },
            { id: "voice", name: qsTr("Voice") }
        ]
        const remote = uses.length === 0 || uses_("calls") || uses_("games") || uses_("stream")
        if (remote)
            s.push({ id: "routing", name: qsTr("Virtual mic") })
        s.push({ id: "hear", name: qsTr("Hearing it") })
        s.push({ id: "dictation", name: qsTr("Dictation") })
        if (remote)
            s.push({ id: "realmic", name: qsTr("Real mic") })
        s.push({ id: "shortcuts", name: qsTr("Shortcuts") })
        s.push({ id: "access", name: qsTr("Comfort") })
        s.push({ id: "look", name: qsTr("Look") })
        if (uses_("stream"))
            s.push({ id: "stream", name: qsTr("Stream") })
        s.push({ id: "practice", name: qsTr("First words") })
        return s
    }
    readonly property var current: steps[Math.min(step, steps.length - 1)]
    readonly property bool last: step >= steps.length - 1

    function next() {
        if (loader.item && loader.item.commit)
            loader.item.commit()
        if (last) {
            onb.finished()
            return
        }
        forward = true
        step = Math.min(steps.length - 1, step + 1)
    }
    function back() {
        forward = false
        step = Math.max(0, step - 1)
    }
    property bool forward: true

    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
            next(); event.accepted = true
        } else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
            back(); event.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Top: brand, where you are, skip -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            spacing: Theme.s3
            Image { source: "qrc:/icons/tray.png"; sourceSize: Qt.size(64, 64); Layout.preferredWidth: Math.round(28 * Theme.scale); Layout.preferredHeight: Layout.preferredWidth }
            Text {
                text: "Vocal Ink"
                font.family: Theme.displayFont
                font.weight: Font.ExtraBold
                font.pixelSize: Math.round(20 * Theme.scale)
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Txt {
                text: qsTr("Step %1 of %2 · %3").arg(onb.step + 1).arg(onb.steps.length).arg(onb.current.name)
                role: "caption"
                font.family: Theme.monoFont
            }
            PillButton {
                visible: !onb.last
                kind: "ghost"
                small: true
                text: qsTr("Skip setup")
                tip: qsTr("You can run it again from Settings → Backup & updates")
                onClicked: onb.finished()
            }
        }

        // --- The progress stroke: the setup is written in ink as you go ------------------
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s4
            implicitHeight: Math.round(30 * Theme.scale)
            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("Setup progress")

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 3
                radius: 1.5
                color: Theme.line
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * (onb.step / Math.max(1, onb.steps.length - 1))
                height: 5
                radius: 2.5
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.5) }
                    GradientStop { position: 1; color: Theme.accent }
                }
                Behavior on width { NumberAnimation { duration: Theme.slow; easing.type: Easing.OutCubic } }
            }
            Repeater {
                model: onb.steps.length
                Rectangle {
                    required property int index
                    readonly property bool done: index < onb.step
                    readonly property bool here: index === onb.step
                    x: (parent.width - width) * index / Math.max(1, onb.steps.length - 1)
                    anchors.verticalCenter: parent.verticalCenter
                    width: here ? 14 : 8
                    height: width
                    radius: width / 2
                    color: done || here ? Theme.accent : Theme.bg
                    border.color: done || here ? Theme.accent : Theme.faint
                    border.width: 2
                    Behavior on width { NumberAnimation { duration: Theme.normal } }
                }
            }
        }

        // --- The step --------------------------------------------------------------------
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: Math.max(height, holder.implicitHeight + Theme.s8 * 2)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: InkScrollBar {}

            Item {
                id: holder
                width: Math.min(flick.width - Theme.s6 * 2, Math.round(820 * Math.min(1.3, Theme.scale)))
                x: (flick.width - width) / 2
                y: Math.max(Theme.s8, (flick.height - implicitHeight) / 2.4)
                implicitHeight: loader.item ? loader.item.implicitHeight : 0

                Loader {
                    id: loader
                    width: parent.width
                    source: "Onb" + onb.current.id.charAt(0).toUpperCase() + onb.current.id.slice(1) + ".qml"
                    onLoaded: {
                        slide.restart()
                        flick.contentY = 0
                        App.announce(onb.current.name)
                    }
                }
                ParallelAnimation {
                    id: slide
                    NumberAnimation { target: loader; property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
                    NumberAnimation { target: loader; property: "x"; from: Theme.travel(onb.forward ? 28 : -28); to: 0; duration: Theme.slow; easing.type: Easing.OutCubic }
                }
            }
        }

        // --- Footer -------------------------------------------------------------------------
        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s4
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            spacing: Theme.s2
            PillButton {
                visible: onb.step > 0
                kind: "ghost"
                iconName: "arrow-left"
                text: qsTr("Back")
                onClicked: onb.back()
            }
            Item { Layout.fillWidth: true }
            Txt {
                visible: loader.item && !loader.item.canContinue
                text: qsTr("Waiting for the download…")
                role: "caption"
            }
            PillButton {
                id: continueButton
                kind: "primary"
                iconName: onb.last ? "check" : "arrow-right"
                text: loader.item && loader.item.continueLabel ? loader.item.continueLabel
                    : onb.step === 0 ? qsTr("Let's set it up")
                    : onb.last ? qsTr("Start talking") : qsTr("Continue")
                shortcut: "Ctrl+Return"
                enabled: !loader.item || loader.item.canContinue
                onClicked: onb.next()
            }
        }
    }
}
