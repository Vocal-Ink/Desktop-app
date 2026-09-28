import QtQuick

// Text in one of the app's type roles.
Text {
    property string role: "body" // caption | label | body | lead | title | heading | hero

    color: role === "caption" ? Theme.muted : Theme.text
    font.family: role === "heading" || role === "hero" ? Theme.displayFont
               : role === "title" ? (Theme.fontChoice === "atkinson" ? "Bricolage Grotesque" : Theme.uiFont)
               : Theme.uiFont
    readonly property real roleSize: role === "caption" ? Theme.fsSm
                  : role === "label" ? Theme.fsMd
                  : role === "lead" ? Theme.fsLg
                  : role === "title" ? Theme.fsXl
                  : role === "heading" ? Theme.fsXxl
                  : role === "hero" ? Theme.fsHero
                  : Theme.fsMd
    font.pixelSize: roleSize
    font.weight: role === "label" ? Font.DemiBold
               : role === "title" ? Font.DemiBold
               : role === "heading" || role === "hero" ? Font.ExtraBold
               : Font.Normal
    font.letterSpacing: Theme.tracking(roleSize)
    lineHeight: role === "heading" || role === "hero" ? 1.0 : Theme.lineHeight
    wrapMode: Text.WordWrap
    textFormat: Text.PlainText
    elide: Text.ElideNone
}
