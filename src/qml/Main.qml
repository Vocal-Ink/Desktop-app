import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Ink.Core

ApplicationWindow {
    id: win
    width: Math.round(1200 * Math.min(1.25, Theme.scale))
    height: Math.round(800 * Math.min(1.2, Theme.scale))
    minimumWidth: 640
    minimumHeight: 480
    visible: !launchMinimized
    title: App.speaking ? qsTr("Speaking — Vocal Ink") : qsTr("Vocal Ink")
    color: Theme.bg
    font.family: Theme.uiFont
    font.pixelSize: Theme.fsMd
    flags: App.prefs["ui/alwaysOnTop"] ? (Qt.Window | Qt.WindowStaysOnTopHint) : Qt.Window

    property string page: "talk"
    property string settingsSection: "appearance"
    readonly property bool onboarding: !App.prefs["ui/onboardingDone"]
    readonly property bool narrow: width < Math.round(860 * Theme.scale)

    function go(p, section) {
        if (p.indexOf(":") > 0) {
            section = p.split(":")[1]
            p = p.split(":")[0]
        }
        if (section)
            settingsSection = section
        page = p
        if (p === "talk")
            Qt.callLater(() => talkLoader.item && talkLoader.item.focusComposer())
    }
    function showWindow() {
        win.show()
        win.raise()
        win.requestActivate()
    }

    Component.onCompleted: launchTimer.start()
    // Open what --show asked for once the main window is up.
    Timer {
        id: launchTimer
        interval: 400
        onTriggered: win.openLaunchPage()
    }
    function openLaunchPage() {
        if (typeof launchPage !== "undefined" && launchPage !== "") {
            if (launchPage.startsWith("onboarding")) {
                onboardingLoader.startAt = launchPage.split(":")[1] || ""
                onboardingLoader.forced = true
            } else if (launchPage === "palette") {
                palette.open()
            } else if (launchPage === "quicktype") {
                quickType.open()
            } else if (launchPage === "showtext") {
                showText.open()
            } else if (launchPage === "compact") {
                compact.toggle()
            } else if (launchPage === "miclive") {
                micOsd.flash(true)
            } else {
                go(launchPage)
            }
        }
    }

    onClosing: (close) => {
        if (App.prefs["ui/minimizeToTray"] && !smokeTest) {
            close.accepted = false
            win.hide()
            App.notifyUser(qsTr("Vocal Ink is still running in the tray so your shortcuts keep working."), 0)
        }
    }

    // --- App events ----------------------------------------------------------------
    Connections {
        target: App
        function onNotify(message, level) { toasts.show(message, level) }
        function onUiAction(id) {
            switch (id) {
            case "window.show": win.showWindow(); break
            case "window.toggle": win.visible && win.active ? win.hide() : win.showWindow(); break
            case "window.quickType": quickType.open(); break
            case "window.compact": compact.toggle(); break
            case "window.showText": showText.open(); break
            case "window.palette": win.showWindow(); palette.open(); break
            }
        }
        function onMicLiveWarning(live, fromShortcut) {
            if (App.prefs["mic/warnOverlay"] !== false)
                micOsd.flash(live)
            if (live && fromShortcut)
                toasts.show(qsTr("Your real microphone is live. People can hear you."), 1)
        }
        function onHotkeysFailed(list) { hotkeyProblem.list = list }
    }

    // --- Local shortcuts -------------------------------------------------------------
    Shortcut { sequences: ["Ctrl+K"]; onActivated: palette.open() }
    Shortcut { sequence: "Ctrl+1"; onActivated: win.go("talk") }
    Shortcut { sequence: "Ctrl+2"; onActivated: win.go("board") }
    Shortcut { sequence: "Ctrl+3"; onActivated: win.go("voices") }
    Shortcut { sequence: "Ctrl+4"; onActivated: win.go("audio") }
    Shortcut { sequence: "Ctrl+5"; onActivated: win.go("stream") }
    Shortcut { sequence: "Ctrl+6"; onActivated: win.go("avatar") }
    Shortcut { sequences: ["Ctrl+,", "Ctrl+7"]; onActivated: win.go("settings") }
    Shortcut { sequence: "F11"; onActivated: showText.open() }
    Shortcut { sequence: "Esc"; enabled: App.speaking && win.page !== "talk"; onActivated: App.stop() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: App.quit() }

    // --- Layout -------------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0
        enabled: !onboardingLoader.active

        Sidebar {
            Layout.fillHeight: true
            current: win.page
            labels: App.prefs["ui/sidebarLabels"] !== false && !win.narrow
            onNavigate: (p) => win.go(p)
            onOpenPalette: palette.open()
        }

        Item {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true

            component PageLoader: Loader {
                property string name
                readonly property bool current: win.page === name
                anchors.fill: parent
                active: current || item !== null
                visible: opacity > 0
                opacity: current ? 1 : 0
                asynchronous: name !== "talk"
                transform: Translate { y: current ? 0 : Theme.travel(10) ; Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }
                Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                onLoaded: if (current && item.forceActiveFocus) item.forceActiveFocus()
            }

            PageLoader {
                id: talkLoader
                name: "talk"
                sourceComponent: TalkPage { onNavigate: (p) => win.go(p) }
            }
            PageLoader { name: "board"; sourceComponent: BoardPage {} }
            PageLoader { name: "voices"; sourceComponent: VoicesPage {} }
            PageLoader { name: "audio"; sourceComponent: AudioPage { onNavigate: (p) => win.go(p) } }
            PageLoader { name: "stream"; sourceComponent: StreamPage {} }
            PageLoader { name: "avatar"; sourceComponent: AvatarPage { onNavigate: (p) => win.go(p) } }
            PageLoader {
                name: "settings"
                sourceComponent: SettingsPage {
                    section: win.settingsSection
                    onSectionChanged: win.settingsSection = section
                    onRunOnboarding: { onboardingLoader.startAt = ""; onboardingLoader.forced = true }
                }
            }

            // A global shortcut didn't register: say which and why, once.
            Rectangle {
                id: hotkeyProblem
                property var list: []
                visible: list.length > 0
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.s3
                width: Math.min(parent.width - Theme.s6, Math.round(460 * Theme.scale))
                height: hk.implicitHeight + Theme.s3 * 2
                radius: Theme.radius
                color: Theme.raised
                border.color: Theme.alpha(Theme.warn, 0.6)
                border.width: Theme.hairline
                RowLayout {
                    id: hk
                    anchors.fill: parent
                    anchors.margins: Theme.s3
                    spacing: Theme.s3
                    Icon { name: "keyboard-off"; color: Theme.warn; Layout.alignment: Qt.AlignTop }
                    Txt {
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fsSm
                        text: qsTr("Another app already uses these shortcuts, so they won't work here: %1").arg(hotkeyProblem.list.join(", "))
                    }
                    PillButton { small: true; kind: "ghost"; text: qsTr("Change"); onClicked: { hotkeyProblem.list = []; win.go("settings", "shortcuts") } }
                    IconButton { small: true; iconName: "x"; tip: qsTr("Dismiss"); onClicked: hotkeyProblem.list = [] }
                }
            }
        }
    }

    Toasts {
        id: toasts
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.s4
        anchors.topMargin: Math.round(76 * Theme.scale)
        z: 50
    }

    CommandPalette {
        id: palette
        onNavigate: (p) => win.go(p)
        onRunOnboarding: { onboardingLoader.startAt = ""; onboardingLoader.forced = true }
    }

    // --- Onboarding: covers everything until it's done -----------------------------------
    Loader {
        id: onboardingLoader
        property string startAt: ""
        property bool forced: false
        anchors.fill: parent
        z: 100
        active: (win.onboarding && !skipOnboarding) || forced
        sourceComponent: Onboarding {
            startAt: onboardingLoader.startAt
            onFinished: {
                App.prefs["ui/onboardingDone"] = true
                onboardingLoader.forced = false
                win.go("talk")
            }
        }
    }

    // --- Other windows ------------------------------------------------------------------
    QuickTypeWindow { id: quickType }
    CompactWindow { id: compact; onOpenMain: win.showWindow() }
    ShowTextWindow { id: showText }
    MicLiveOsd { id: micOsd }
}
