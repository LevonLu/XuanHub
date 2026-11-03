// UiButton.qml — FluentWinUI3 按钮，保留插件共用的形态与紧凑接口
import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent
import XuanHub

Fluent.Button {
    property string variant: "outline"
    property bool compact: false
    highlighted: variant === "primary"
    flat: variant === "ghost"
    font.pixelSize: compact ? UiMetrics.fontS : UiMetrics.fontM
    implicitHeight: compact ? UiMetrics.controlHeightS : UiMetrics.controlHeightM
}
