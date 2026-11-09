// MijiaIconButton.qml — 原生 Fluent 图标按钮，沿用宿主的悬停、焦点与选中样式
import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent

Fluent.Button {
    id: control
    property string glyph: "generic"
    property string hint: ""
    property color tint: palette.buttonText
    flat: true
    implicitWidth: 32
    implicitHeight: 32
    padding: 8
    leftPadding:padding;rightPadding:padding;topPadding:padding;bottomPadding:padding
    leftInset:0;rightInset:0;topInset:0;bottomInset:0
    Accessible.name: hint
    Fluent.ToolTip.visible: hovered && hint.length > 0
    Fluent.ToolTip.text: hint
    contentItem: DeviceGlyph {
        kind:control.glyph
        tint:control.highlighted?control.palette.highlightedText:control.tint
        opacity:control.enabled?1:0.4
    }
}
