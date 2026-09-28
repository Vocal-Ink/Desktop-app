import QtQuick
import QtQuick.Controls.Basic
import Ink.Core

// The interface language. Follows the computer unless the user picks one.
// Each language is listed in its own name (plus English), so someone who
// switched by accident can always find their way back.
Choice {
    id: root
    readonly property var lang: App.language
    label: qsTr("Language")
    implicitWidth: Math.round(300 * Theme.scale)
    textRole: "text"
    valueRole: "value"
    readonly property var entries: {
        if (!lang)
            return []
        const list = [{ value: "", text: qsTr("Match my computer (%1)").arg(nameOf(lang.system)) }]
        for (const l of lang.available)
            list.push({ value: l.code, text: l.name === l.english ? l.name : l.name + "  ·  " + l.english })
        return list
    }
    model: entries
    currentIndex: lang ? Math.max(0, entries.findIndex(e => e.value === lang.choice)) : 0
    onActivated: (i) => {
        lang.choose(entries[i].value)
        // Switching retranslates the whole interface; stay on this control.
        Qt.callLater(root.forceActiveFocus)
    }

    function nameOf(code) {
        for (const l of lang.available)
            if (l.code === code)
                return l.name
        return code
    }
}
