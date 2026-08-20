// UiComboBox.qml — 自绘下拉框：圆角描边 + 箭头 + popup 项 hover 态，默认充满布局列宽
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts   // 根上的 Layout.fillWidth 附件属性需要本模块
import XuanHub

ComboBox {
    id: control

    font.pixelSize: UiMetrics.fontM
    Layout.fillWidth: true   // 表单控件列默认充满（不在布局中时该附件属性被忽略）
    opacity: enabled ? 1.0 : 0.5

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.palette.buttonText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: UiMetrics.space3 - 2
        rightPadding: control.indicator.width + UiMetrics.space2
    }

    indicator: Text {
        text: "▾"
        color: control.palette.placeholderText
        font.pixelSize: UiMetrics.fontS
        x: control.width - width - UiMetrics.space3 + 2
        y: control.height / 2 - height / 2
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: UiMetrics.controlHeightM
        radius: UiMetrics.radiusS
        color: Qt.rgba(control.palette.base.r, control.palette.base.g, control.palette.base.b, 0.86)
        border.width: 1
        border.color: control.popup.visible || control.activeFocus
                      ? control.palette.highlight
                      : Qt.rgba(control.palette.mid.r, control.palette.mid.g,
                                control.palette.mid.b, 0.72)
        Behavior on border.color { ColorAnimation { duration: UiMetrics.animFast } }
    }

    delegate: ItemDelegate {
        id: item
        width: ListView.view ? ListView.view.width : control.width
        highlighted: control.highlightedIndex === index
        // 注意：popup 是独立顶层窗口，palette 继承链在窗口边界断裂，
        // item.palette 会回落到系统 palette（深色模式下是深文字配深背景，
        // 实测几乎不可见）——弹出项一律用主窗口内 control 的 palette
        contentItem: Text {
            // 本窗口模型均为字符串列表（JS 数组 / QStringList），modelData 即显示文本；
            // textRole 分支仅作完整性兜底
            text: control.textRole
                  ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole])
                  : modelData
            font: control.font
            color: control.palette.buttonText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: UiMetrics.radiusS - 2
            readonly property color acc: control.palette.highlight
            readonly property color fg: control.palette.buttonText
            color: item.highlighted ? Qt.rgba(acc.r, acc.g, acc.b, 0.16)
                 : item.hovered ? Qt.rgba(fg.r, fg.g, fg.b, 0.06) : "transparent"
            Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        }
    }

    popup: Popup {
        y: control.height + UiMetrics.space1
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight, 240) + topPadding + bottomPadding
        padding: UiMetrics.space1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: control.palette.window
            radius: UiMetrics.radiusM
            border.width: 1
            border.color: control.palette.mid
        }
    }
}
