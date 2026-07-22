// UiCard.qml — 分组卡片：标题 + 可选描述 + 内容区（default 子项进内容列），panel 底 radius 8
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Pane {
    id: card

    property string title: ""
    property string description: ""
    // 卡片内容：default 子项落到 body 列（列间距统一 12）
    default property alias content: body.data

    padding: 14

    background: Rectangle {
        color: card.palette.button
        radius: UiMetrics.radiusM
        border.width: 1
        // 细分隔边：从 palette.mid 派生半透明，不新增角色
        border.color: Qt.rgba(card.palette.mid.r, card.palette.mid.g, card.palette.mid.b, 0.45)
    }

    contentItem: ColumnLayout {
        spacing: UiMetrics.space2

        Text {
            visible: card.title.length > 0
            text: card.title
            color: card.palette.windowText
            font.pixelSize: UiMetrics.fontL
            font.bold: true
        }
        Text {
            visible: card.description.length > 0
            text: card.description
            color: card.palette.placeholderText
            font.pixelSize: UiMetrics.fontS
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ColumnLayout {
            id: body
            spacing: UiMetrics.space3
            Layout.fillWidth: true
            Layout.topMargin: card.title.length > 0 ? UiMetrics.space1 : 0
        }
    }
}
