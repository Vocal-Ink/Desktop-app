import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    id: page
    title: qsTr("Appearance")
    subtitle: qsTr("Make Vocal Ink look and feel the way you like. Changes apply as you make them.")

    // --- Saved looks: every appearance setting under a name -----------------------
    readonly property var lookKeys: [
        "ui/theme", "ui/accent", "ui/backgroundTint", "ui/paperTexture", "ui/surfaceStyle", "ui/shadowDepth",
        "ui/font", "ui/fontScale", "ui/stageScale", "ui/stageFont", "ui/stageAlign", "ui/composerSize",
        "ui/density", "ui/corners", "ui/sidebarLabels", "ui/sidebarSide", "ui/headerChips", "ui/composerPosition",
        "ui/showPhraseTray", "ui/waveStyle", "ui/inkEffect", "ui/motion",
        "a11y/focusRing", "a11y/letterSpacing", "a11y/lineSpacing", "a11y/largeTargets"
    ]
    readonly property var looks: {
        try {
            const list = JSON.parse(App.prefs["ui/looks"] || "[]")
            return Array.isArray(list) ? list.filter(l => l && typeof l.name === "string" && typeof l.values === "object") : []
        } catch (e) {
            return []
        }
    }
    function currentValues() {
        const values = {}
        for (const k of lookKeys)
            values[k] = App.prefs[k]
        return values
    }
    function saveLooks(list) { App.prefs["ui/looks"] = JSON.stringify(list) }
    function saveLook(name) {
        name = name.trim()
        if (name === "")
            return
        const list = looks.filter(l => l.name !== name)
        list.push({ name: name, values: currentValues() })
        saveLooks(list)
        App.notifyUser(qsTr("Saved the look “%1”.").arg(name), 0)
    }
    function applyLook(look) {
        for (const k of lookKeys) {
            const v = look.values[k]
            if (v === undefined || v === null || typeof v === "object")
                continue
            if (k === "ui/accent" && !/^#[0-9a-fA-F]{6}$/.test(v))
                continue // files can come from anywhere
            App.prefs[k] = v
        }
    }
    function contrast(a, b) {
        const l1 = Theme.luminance(a), l2 = Theme.luminance(b)
        return (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05)
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Language")
        iconName: "languages"
        SettingRow {
            title: qsTr("Interface language")
            description: qsTr("Menus, buttons and messages. Your voice keeps speaking whatever language you type.")
            LanguagePicker {}
        }
        Txt {
            Layout.fillWidth: true
            visible: App.language && App.language.current !== "en"
            role: "caption"
            //: Shown under the language picker when a translation is in use.
            text: qsTr("This translation was made with the help of AI and hasn't been checked by a native speaker yet. If something reads oddly, tell us on GitHub.")
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Theme")
        iconName: "palette"
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s3
            Repeater {
                model: [
                    { id: "midnight", label: qsTr("Midnight ink") },
                    { id: "vellum", label: qsTr("Vellum (light)") },
                    { id: "amethyst", label: qsTr("Amethyst (true black)") },
                    { id: "contrast", label: qsTr("High contrast") },
                    { id: "system", label: qsTr("Match my system") }
                ]
                ThemeCard {
                    required property var modelData
                    themeId: modelData.id
                    label: modelData.label
                    selected: (App.prefs["ui/theme"] || "midnight") === modelData.id
                    onClicked: App.prefs["ui/theme"] = modelData.id
                }
            }
        }
        SettingRow {
            title: qsTr("Ink colour")
            description: Theme.highContrast ? qsTr("High contrast uses its own colours.") : qsTr("Used for the words being spoken, buttons and highlights.")
            Row {
                spacing: Theme.s1 + 2
                Repeater {
                    model: Theme.accents
                    Swatch {
                        required property var modelData
                        swatch: modelData.color
                        name: modelData.name
                        enabled: !Theme.highContrast
                        selected: (App.prefs["ui/accent"] || "#8c52ff").toLowerCase() === modelData.color.toLowerCase()
                        onClicked: App.prefs["ui/accent"] = modelData.color
                    }
                }
            }
        }
        SettingRow {
            visible: !Theme.highContrast
            title: qsTr("Your own ink colour")
            description: page.contrast(Theme.accent, Theme.bg) < 3
                ? qsTr("This colour is hard to see on this theme. Buttons may be hard to find.")
                : qsTr("Any colour, as a hex code or from the palette.")
            ColorPick {
                label: qsTr("Your own ink colour")
                value: (App.prefs["ui/accent"] || "#8c52ff").toLowerCase()
                onPicked: (c) => App.prefs["ui/accent"] = c
            }
        }
        SettingRow {
            visible: !Theme.highContrast
            title: qsTr("Background tint")
            description: qsTr("Washes a little of your ink colour through the background.")
            ValueSlider {
                label: qsTr("Background tint")
                from: 0; to: 100; stepSize: 5; suffix: "%"
                value: App.prefs["ui/backgroundTint"] || 0
                onMoved: App.prefs["ui/backgroundTint"] = value
            }
        }
        SettingRow {
            visible: !Theme.highContrast
            title: qsTr("Paper texture")
            description: qsTr("A faint grain, like ink on paper.")
            ValueSlider {
                label: qsTr("Paper texture")
                from: 0; to: 100; stepSize: 5; suffix: "%"
                value: App.prefs["ui/paperTexture"] || 0
                onMoved: App.prefs["ui/paperTexture"] = value
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Text")
        iconName: "type"
        SettingRow {
            title: qsTr("Font")
            description: qsTr("Atkinson Hyperlegible was designed for low vision. OpenDyslexic and Lexend can make reading easier with dyslexia.")
            stacked: true
            Segmented {
                label: qsTr("Font")
                value: App.prefs["ui/font"] || "atkinson"
                options: [
                    { value: "atkinson", label: "Atkinson Hyperlegible" },
                    { value: "lexend", label: "Lexend" },
                    { value: "opendyslexic", label: "OpenDyslexic" },
                    { value: "system", label: qsTr("System") }
                ]
                onActivated: (v) => App.prefs["ui/font"] = v
            }
        }
        SettingRow {
            title: qsTr("Text size")
            ValueSlider {
                label: qsTr("Text size")
                from: 80; to: 250; stepSize: 5; suffix: "%"
                value: App.prefs["ui/fontScale"]
                // Apply on release: resizing everything while dragging is jumpy.
                onPressedChanged: if (!pressed) App.prefs["ui/fontScale"] = value
                Keys.onReleased: App.prefs["ui/fontScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Size of the line being spoken")
            ValueSlider {
                label: qsTr("Spoken line size")
                from: 60; to: 200; stepSize: 5; suffix: "%"
                value: App.prefs["ui/stageScale"]
                onMoved: App.prefs["ui/stageScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Message box text size")
            ValueSlider {
                label: qsTr("Message box text size")
                from: 12; to: 40; suffix: " pt"
                value: App.prefs["ui/composerSize"]
                onMoved: App.prefs["ui/composerSize"] = value
            }
        }
        // A live sample, so the choices above are judged on real words.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: sample.implicitHeight + Theme.s4 * 2
            radius: Theme.radius
            color: Theme.sunken
            InkLine {
                id: sample
                x: Theme.s4
                y: Theme.s4
                width: parent.width - Theme.s4 * 2
                text: qsTr("The quick brown fox jumps over the lazy dog.")
                progress: 0.55
                animate: true
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Layout")
        iconName: "layout-grid"
        SettingRow {
            title: qsTr("Spacing")
            Segmented {
                label: qsTr("Spacing")
                value: App.prefs["ui/density"] || "comfortable"
                options: [{ value: "compact", label: qsTr("Compact") }, { value: "comfortable", label: qsTr("Comfortable") }, { value: "spacious", label: qsTr("Spacious") }]
                onActivated: (v) => App.prefs["ui/density"] = v
            }
        }
        SettingRow {
            title: qsTr("Corners")
            Segmented {
                label: qsTr("Corners")
                value: App.prefs["ui/corners"] || "soft"
                options: [{ value: "sharp", label: qsTr("Sharp") }, { value: "soft", label: qsTr("Soft") }, { value: "round", label: qsTr("Round") }]
                onActivated: (v) => App.prefs["ui/corners"] = v
            }
        }
        SettingRow {
            title: qsTr("Cards")
            Segmented {
                label: qsTr("Cards")
                value: App.prefs["ui/surfaceStyle"] || "filled"
                options: [{ value: "filled", label: qsTr("Filled") }, { value: "outlined", label: qsTr("Outlined") }, { value: "flat", label: qsTr("Flat") }]
                onActivated: (v) => App.prefs["ui/surfaceStyle"] = v
            }
        }
        SettingRow {
            visible: (App.prefs["ui/surfaceStyle"] || "filled") !== "outlined"
            title: qsTr("Shadows")
            ValueSlider {
                label: qsTr("Shadows")
                from: 0; to: 100; stepSize: 5; suffix: "%"
                value: App.prefs["ui/shadowDepth"] !== undefined ? App.prefs["ui/shadowDepth"] : 40
                onMoved: App.prefs["ui/shadowDepth"] = value
            }
        }
        SettingRow {
            title: qsTr("Sidebar side")
            Segmented {
                label: qsTr("Sidebar side")
                value: App.prefs["ui/sidebarSide"] || "left"
                options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }]
                onActivated: (v) => App.prefs["ui/sidebarSide"] = v
            }
        }
        SettingRow {
            title: qsTr("Sidebar labels")
            Toggle { tip: qsTr("Sidebar labels"); checked: App.prefs["ui/sidebarLabels"] !== false; onToggled: App.prefs["ui/sidebarLabels"] = checked }
        }
        SettingRow {
            title: qsTr("Quick phrases under the message box")
            Toggle { tip: qsTr("Quick phrases under the message box"); checked: App.prefs["ui/showPhraseTray"] !== false; onToggled: App.prefs["ui/showPhraseTray"] = checked }
        }
        SettingRow {
            title: qsTr("Voice drawing")
            description: qsTr("How your voice is drawn under the message box.")
            Segmented {
                label: qsTr("Voice drawing")
                value: App.prefs["ui/waveStyle"] || "ink"
                options: [{ value: "ink", label: qsTr("Brush stroke") }, { value: "bars", label: qsTr("Bars") }, { value: "off", label: qsTr("Off") }]
                onActivated: (v) => App.prefs["ui/waveStyle"] = v
            }
        }
        SettingRow {
            title: qsTr("Fill words with ink as they're spoken")
            description: qsTr("Helps you and the people around you follow along.")
            Toggle { tip: qsTr("Fill words with ink as they're spoken"); checked: App.prefs["ui/inkEffect"] !== false; onToggled: App.prefs["ui/inkEffect"] = checked }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Talk page")
        iconName: "message-square-text"
        SettingRow {
            title: qsTr("Spoken lines")
            Segmented {
                label: qsTr("Spoken lines")
                value: App.prefs["ui/stageAlign"] || "left"
                options: [{ value: "left", label: qsTr("Left") }, { value: "center", label: qsTr("Centred") }]
                onActivated: (v) => App.prefs["ui/stageAlign"] = v
            }
        }
        SettingRow {
            title: qsTr("Font of the spoken lines")
            Segmented {
                label: qsTr("Font of the spoken lines")
                value: App.prefs["ui/stageFont"] || "display"
                options: [{ value: "display", label: qsTr("Display") }, { value: "reading", label: qsTr("Reading font") }, { value: "serif", label: qsTr("Serif") }]
                onActivated: (v) => App.prefs["ui/stageFont"] = v
            }
        }
        SettingRow {
            title: qsTr("Message box")
            Segmented {
                label: qsTr("Message box")
                value: App.prefs["ui/composerPosition"] || "bottom"
                options: [{ value: "bottom", label: qsTr("At the bottom") }, { value: "top", label: qsTr("At the top") }]
                onActivated: (v) => App.prefs["ui/composerPosition"] = v
            }
        }
        SettingRow {
            title: qsTr("Status in the header")
            description: qsTr("Where your voice goes and whether the real mic is on. A live mic always shows.")
            Toggle { tip: qsTr("Status in the header"); checked: App.prefs["ui/headerChips"] !== false; onToggled: App.prefs["ui/headerChips"] = checked }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Saved looks")
        subtitle: qsTr("Keep a look you like, switch in one click, or share it as a file.")
        iconName: "layers"
        Txt {
            Layout.fillWidth: true
            visible: page.looks.length === 0
            role: "caption"
            text: qsTr("No saved looks yet. Set things up the way you like, then save them here.")
        }
        Repeater {
            model: page.looks
            RowLayout {
                id: lookRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: Theme.s2
                Rectangle {
                    Layout.preferredWidth: Math.round(22 * Theme.scale)
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: lookRow.modelData.values["ui/accent"] || "#8c52ff"
                    border.color: Theme.line
                }
                Txt { text: lookRow.modelData.name; role: "label"; Layout.fillWidth: true; elide: Text.ElideRight; wrapMode: Text.NoWrap }
                PillButton { small: true; kind: "primary"; text: qsTr("Use"); onClicked: page.applyLook(lookRow.modelData) }
                IconButton {
                    iconName: "file-down"
                    tip: qsTr("Export to a file")
                    onClicked: { exportDialog.look = lookRow.modelData; exportDialog.open() }
                }
                IconButton {
                    iconName: "trash-2"
                    tip: qsTr("Delete this look")
                    onClicked: page.saveLooks(page.looks.filter((l, i) => i !== lookRow.index))
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            Field {
                id: lookName
                Layout.fillWidth: true
                label: qsTr("Name for this look")
                placeholderText: qsTr("Name for this look, e.g. Stream night")
                onAccepted: { page.saveLook(text); text = "" }
            }
            PillButton { text: qsTr("Save current look"); iconName: "plus"; enabled: lookName.text.trim() !== ""; onClicked: { page.saveLook(lookName.text); lookName.text = "" } }
            PillButton { kind: "ghost"; text: qsTr("Import…"); iconName: "file-up"; onClicked: importDialog.open() }
        }
        FileDialog {
            id: exportDialog
            property var look: null
            title: qsTr("Export look")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "vilook"
            nameFilters: [qsTr("Vocal Ink looks (*.vilook)")]
            onAccepted: {
                const ok = App.writeTextFile(selectedFile, JSON.stringify({ app: "Vocal Ink", kind: "look", name: look.name, values: look.values }, null, 2))
                App.notifyUser(ok ? qsTr("Look exported.") : qsTr("Couldn't write that file."), ok ? 0 : 1)
            }
        }
        FileDialog {
            id: importDialog
            title: qsTr("Import a look")
            nameFilters: [qsTr("Vocal Ink looks (*.vilook)"), qsTr("All files (*)")]
            onAccepted: {
                let look = null
                try { look = JSON.parse(App.readTextFile(selectedFile)) } catch (e) {}
                if (!look || typeof look.name !== "string" || typeof look.values !== "object") {
                    App.notifyUser(qsTr("That file isn't a Vocal Ink look."), 1)
                    return
                }
                // Only appearance settings come in, whatever else the file holds.
                const values = {}
                for (const k of page.lookKeys)
                    if (look.values[k] !== undefined)
                        values[k] = look.values[k]
                const list = page.looks.filter(l => l.name !== look.name)
                list.push({ name: look.name.slice(0, 60), values: values })
                page.saveLooks(list)
                App.notifyUser(qsTr("Imported “%1”. Choose Use to apply it.").arg(look.name.slice(0, 60)), 0)
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Motion & windows")
        iconName: "app-window"
        SettingRow {
            title: qsTr("Animation")
            description: qsTr("Reduced keeps fades but drops movement.")
            Segmented {
                label: qsTr("Animation")
                value: App.prefs["ui/motion"] || "full"
                options: [{ value: "full", label: qsTr("Full") }, { value: "reduced", label: qsTr("Reduced") }, { value: "off", label: qsTr("Off") }]
                onActivated: (v) => App.prefs["ui/motion"] = v
            }
        }
        SettingRow {
            title: qsTr("Keep Vocal Ink above other windows")
            Toggle { tip: qsTr("Keep above other windows"); checked: App.prefs["ui/alwaysOnTop"] === true; onToggled: App.prefs["ui/alwaysOnTop"] = checked }
        }
        SettingRow {
            title: qsTr("Closing the window keeps it running in the tray")
            description: qsTr("So your shortcuts keep working.")
            Toggle { tip: qsTr("Keep running in the tray"); checked: App.prefs["ui/minimizeToTray"] !== false; onToggled: App.prefs["ui/minimizeToTray"] = checked }
        }
        SettingRow {
            title: qsTr("Compact bar opacity")
            ValueSlider {
                label: qsTr("Compact bar opacity")
                from: 40; to: 100; suffix: "%"
                value: App.prefs["ui/compactOpacity"]
                onMoved: App.prefs["ui/compactOpacity"] = value
            }
        }
    }
}
