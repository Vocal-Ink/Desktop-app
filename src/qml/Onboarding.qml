import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// First-run setup, full window: a journey along one ink stroke. Each step is
// a full-width slide; Continue glides to the next node. Every step can be
// skipped and revisited later; nothing here is required to start talking.
Rectangle {
    id: onb
    // Where to start: a step id ("voice", "avatar") or a number. An id that the
    // route would normally skip (e.g. "stream" without streaming) is added.
    property string startAt: ""
    signal finished()

    color: Theme.bg
    focus: true
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Vocal Ink setup")

    // --- The route --------------------------------------------------------------
    // Ids only; the words come from title() so a language switch never
    // rebuilds the slides.
    readonly property var uses: App.prefs["ui/uses"] || []
    property string forcedId: ""
    function using(u) { return uses.indexOf(u) >= 0 }
    readonly property var route: {
        const remote = uses.length === 0 || using("calls") || using("games") || using("stream") || using("vtubing")
        const r = ["welcome", "uses", "voice"]
        if (remote || forcedId === "routing") r.push("routing")
        r.push("hear", "dictation")
        if (remote || forcedId === "realmic") r.push("realmic")
        if (using("vtubing") || forcedId === "avatar") r.push("avatar")
        r.push("shortcuts", "look")
        if (using("stream") || forcedId === "stream") r.push("stream")
        r.push("practice")
        return r
    }
    function title(id) {
        switch (id) {
        case "welcome": return qsTr("Welcome")
        case "uses": return qsTr("Your day")
        case "voice": return qsTr("Voice")
        case "routing": return qsTr("Virtual mic")
        case "hear": return qsTr("Hearing it")
        case "dictation": return qsTr("Dictation")
        case "realmic": return qsTr("Real mic")
        case "avatar": return qsTr("Avatar")
        case "shortcuts": return qsTr("Shortcuts")
        case "look": return qsTr("Look")
        case "stream": return qsTr("Stream")
        case "practice": return qsTr("First words")
        }
        return id
    }
    function fileOf(id) { return "Onb" + id.charAt(0).toUpperCase() + id.slice(1) + ".qml" }
    readonly property var labels: route.map(id => title(id))

    // --- Where we are ------------------------------------------------------------
    property int step: 0
    property int reached: 0
    readonly property bool last: step >= route.length - 1
    readonly property string currentId: route[Math.min(step, route.length - 1)]
    // The camera: glides to `step`; fractional while travelling.
    property real pos: step
    property bool jumping: false
    Behavior on pos {
        enabled: !onb.jumping
        NumberAnimation { duration: Theme.dur(620); easing.type: Easing.OutQuart }
    }
    readonly property bool travelling: Math.abs(pos - step) > 0.001

    Component.onCompleted: applyStart()
    onStartAtChanged: applyStart()
    function applyStart() {
        const n = parseInt(startAt)
        let target = 0
        if (!isNaN(n)) {
            target = Math.max(0, Math.min(n, route.length - 1))
        } else if (startAt !== "") {
            forcedId = startAt
            target = Math.max(0, route.indexOf(startAt))
        }
        // No glide to the starting slide (and never assign pos: it follows step).
        jumping = true
        step = target
        reached = Math.max(reached, target)
        jumping = false
    }
    onStepChanged: {
        reached = Math.max(reached, step)
        App.announce(qsTr("Step %1 of %2: %3").arg(step + 1).arg(route.length).arg(title(currentId)))
    }
    onTravellingChanged: if (!travelling) focusTimer.restart()
    Timer { id: focusTimer; interval: 1; onTriggered: onb.focusSlide() }

    function currentSlide() {
        for (let i = 0; i < slots.count; ++i) {
            const s = slots.itemAt(i)
            if (s && s.stepIndex === step)
                return s
        }
        return null
    }
    // The step on screen (its canContinue, continueLabel, commit()).
    property var activeItem: null
    function currentItem() { return activeItem }
    // Put keyboard focus on the slide's first control once it has arrived.
    function focusSlide() {
        const s = currentSlide()
        if (!s || !s.content)
            return
        const item = s.content
        if (item.firstFocus) {
            item.firstFocus.forceActiveFocus()
            return
        }
        let f = s.nextItemInFocusChain(true)
        for (let p = f; p; p = p.parent) {
            if (p === s) {
                f.forceActiveFocus()
                return
            }
        }
        s.forceActiveFocus()
    }

    function next() {
        const item = currentItem()
        if (item && item.commit)
            item.commit()
        if (last) {
            onb.finished()
            return
        }
        step = Math.min(route.length - 1, step + 1)
    }
    function back() {
        if (step > 0)
            step = step - 1
    }
    // Neighbours glide; longer jumps fade across instead of racing past slides.
    function goTo(index) {
        if (index < 0 || index > reached || index === step)
            return
        if (Math.abs(index - step) <= 1 || !Theme.motionOn) {
            step = index
            return
        }
        jumpTo = index
        jumpAnim.restart()
    }
    property int jumpTo: 0
    SequentialAnimation {
        id: jumpAnim
        NumberAnimation { target: strip; property: "opacity"; to: 0; duration: Theme.fast }
        ScriptAction {
            script: {
                onb.jumping = true
                onb.step = onb.jumpTo
                onb.jumping = false
            }
        }
        NumberAnimation { target: strip; property: "opacity"; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
    }

    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
            if (continueButton.enabled) next()
            event.accepted = true
        } else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
            back(); event.accepted = true
        } else if (event.key === Qt.Key_Right && (event.modifiers & Qt.AltModifier)) {
            if (continueButton.enabled) next()
            event.accepted = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Brand and a way out ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s4
            spacing: Theme.s3
            Image { source: "qrc:/icons/tray.png"; sourceSize: Qt.size(64, 64); Layout.preferredWidth: Math.round(26 * Theme.scale); Layout.preferredHeight: Layout.preferredWidth }
            Text {
                text: "Vocal Ink"
                font.family: Theme.displayFont
                font.weight: Font.ExtraBold
                font.pixelSize: Math.round(19 * Theme.scale)
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            PillButton {
                visible: !onb.last
                kind: "ghost"
                small: true
                text: qsTr("Skip setup")
                tip: qsTr("You can run it again from Settings → Backup & updates")
                onClicked: onb.finished()
            }
        }

        JourneyHeader {
            id: journey
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            labels: onb.labels
            current: onb.step
            reached: onb.reached
            position: onb.pos
            onJump: (i) => onb.goTo(i)
        }

        // --- The slides: a strip the window travels along -----------------------------
        Item {
            id: strip
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // Three slots hold the current step and its neighbours. Moving
            // one step keeps the slide you're on alive (its state and focus);
            // only the slot that falls out of reach loads a new step.
            Repeater {
                id: slots
                model: 3
                delegate: FocusScope {
                    id: slot
                    required property int index
                    readonly property int stepIndex: {
                        const c = Math.round(onb.pos)
                        for (let d = -1; d <= 1; ++d) {
                            const t = c + d
                            if (((t % 3) + 3) % 3 === index)
                                return t
                        }
                        return -1
                    }
                    readonly property string stepId: stepIndex >= 0 && stepIndex < onb.route.length ? onb.route[stepIndex] : ""
                    readonly property bool isCurrent: stepIndex === onb.step
                    readonly property real offset: stepIndex - onb.pos
                    readonly property var content: loader.item
                    readonly property bool owns: isCurrent && loader.status === Loader.Ready
                    onOwnsChanged: if (owns) onb.activeItem = loader.item

                    width: strip.width
                    height: strip.height
                    x: offset * strip.width
                    visible: stepId !== "" && Math.abs(offset) < 0.999
                    enabled: isCurrent && !onb.travelling
                    opacity: 1 - Math.min(1, Math.abs(offset)) * 0.7
                    Accessible.role: Accessible.Grouping
                    Accessible.name: onb.title(stepId)

                    Flickable {
                        id: flick
                        anchors.fill: parent
                        contentHeight: Math.max(height, holder.implicitHeight + Theme.s6 * 2)
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: InkScrollBar {}

                        Item {
                            id: holder
                            width: Math.min(flick.width - Theme.s6 * 2, Math.round(1180 * Math.min(1.3, Theme.scale)))
                            x: (flick.width - width) / 2
                            y: Math.max(Theme.s6, (flick.height - implicitHeight) / 2.6)
                            implicitHeight: loader.item ? loader.item.implicitHeight : 0

                            Loader {
                                id: loader
                                width: parent.width
                                source: slot.stepId !== "" ? onb.fileOf(slot.stepId) : ""
                                asynchronous: !slot.isCurrent
                                onLoaded: flick.contentY = 0
                            }
                        }
                    }
                }
            }
        }

        // --- Footer -----------------------------------------------------------------------
        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s4
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            spacing: Theme.s3
            PillButton {
                visible: onb.step > 0
                kind: "ghost"
                iconName: "arrow-left"
                text: qsTr("Back")
                onClicked: onb.back()
            }
            Item { Layout.fillWidth: true }
            Txt {
                readonly property var item: onb.activeItem
                visible: !!item && item.canContinue === false
                text: item && item.waitingText ? item.waitingText : qsTr("Waiting for the download…")
                role: "caption"
            }
            PillButton {
                id: continueButton
                readonly property var item: onb.activeItem
                kind: "primary"
                iconName: onb.last ? "check" : "arrow-right"
                text: item && item.continueLabel ? item.continueLabel
                    : onb.step === 0 ? qsTr("Let's set it up")
                    : onb.last ? qsTr("Start talking") : qsTr("Continue")
                shortcut: "Ctrl+Return"
                enabled: (!item || item.canContinue !== false) && !jumpAnim.running
                onClicked: onb.next()
            }
        }
    }
}
