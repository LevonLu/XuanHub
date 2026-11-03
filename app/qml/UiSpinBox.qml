// UiSpinBox.qml — FluentWinUI3 数字框，保留编辑、校验及 valueModified 语义
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Layouts
import XuanHub

Fluent.SpinBox {
    editable: true
    font.pixelSize: UiMetrics.fontM
    Layout.fillWidth: true
}
