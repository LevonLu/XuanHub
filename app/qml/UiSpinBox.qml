// UiSpinBox.qml — 自绘数字框：圆角描边（焦点 accent）+ ± 指示钮；contentItem 保留 Basic 默认
// （自己重写 TextInput 会丢键盘编辑/校验行为，只覆盖 background 与两个 indicator）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts   // 根上的 Layout.fillWidth 附件属性需要本模块
import XuanHub

SpinBox {
    id: control

    editable: true
    font.pixelSize: UiMetrics.fontM
    Layout.fillWidth: true
    opacity: enabled ? 1.0 : 0.5

    background: Rectangle {
        implicitWidth: 96
        implicitHeight: UiMetrics.controlHeightM
        radius: UiMetrics.radiusS
        color: Qt.rgba(control.palette.base.r, control.palette.base.g, control.palette.base.b, 0.86)
        border.width: 1
        border.color: control.activeFocus ? control.palette.highlight
                      : Qt.rgba(control.palette.mid.r, control.palette.mid.g,
                                control.palette.mid.b, 0.72)
        Behavior on border.color { ColorAnimation { duration: UiMetrics.animFast } }
    }

    up.indicator: Rectangle {
        x: control.width - width
        width: 28
        height: control.height / 2
        readonly property color fg: control.palette.buttonText
        color: control.up.pressed ? Qt.rgba(fg.r, fg.g, fg.b, 0.16)
             : control.up.hovered ? Qt.rgba(fg.r, fg.g, fg.b, 0.08) : "transparent"
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.palette.buttonText
            font.pixelSize: UiMetrics.fontS
        }
    }

    down.indicator: Rectangle {
        x: control.width - width
        y: control.height / 2
        width: 28
        height: control.height / 2
        readonly property color fg: control.palette.buttonText
        color: control.down.pressed ? Qt.rgba(fg.r, fg.g, fg.b, 0.16)
             : control.down.hovered ? Qt.rgba(fg.r, fg.g, fg.b, 0.08) : "transparent"
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        Text {
            anchors.centerIn: parent
            text: "−"
            color: control.palette.buttonText
            font.pixelSize: UiMetrics.fontS
        }
    }
}
