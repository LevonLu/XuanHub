// UiCard.qml — 分组卡片：标题 + 可选描述 + 内容区（default 子项进内容列），panel 底 radius 8
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Pane {
    id: card

    property string title: ""
    property string description: ""
    property bool compact: false
    // 卡片内容：default 子项落到 body 列（列间距统一 12）
    default property alias content: body.data

    padding: compact ? UiMetrics.space4 : UiMetrics.space5

    background: Rectangle {
        // 浅色主题下略透出窗口底色，深色主题下保留足够层次；避免旧版厚重灰框。
        color: Qt.rgba(card.palette.button.r, card.palette.button.g, card.palette.button.b, 0.72)
        radius: UiMetrics.radiusM
        border.width: 1
        border.color: Qt.rgba(card.palette.mid.r, card.palette.mid.g, card.palette.mid.b, 0.28)
    }

    contentItem: ColumnLayout {
        spacing: card.compact ? UiMetrics.space1 : UiMetrics.space2

        Text {
            visible: card.title.length > 0
            text: card.title
            color: card.palette.windowText
            font.pixelSize: UiMetrics.fontL
            font.weight: Font.DemiBold
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
            spacing: card.compact ? UiMetrics.space2 : UiMetrics.space3
            Layout.fillWidth: true
            Layout.topMargin: card.title.length > 0
                              ? (card.compact ? UiMetrics.space1 : UiMetrics.space2) : 0
        }
    }
}
