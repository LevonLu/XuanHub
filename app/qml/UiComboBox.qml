// UiComboBox.qml — FluentWinUI3 下拉框，兼容字符串与对象模型并传递弹层色板
import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Layouts
import XuanHub

Fluent.ComboBox {
    id: control
    font.pixelSize: UiMetrics.fontM
    Layout.fillWidth: true

    // 弹层跨窗口后不能依靠隐式色板继承；保留完整色板及 Qt 自带的滚动和定位。
    popup.palette: control.palette
    delegate: Fluent.ItemDelegate {
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        text: control.textAt(index)
        palette: control.palette
        font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }
}
