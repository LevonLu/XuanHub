// UiCard.qml — Fluent 分组卡片，支持保留编辑状态的折叠内容
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Pane {
    id: card

    property string title: ""
    property string description: ""
    property bool compact: false
    property bool collapsible: false
    property bool expanded: false
    property string summary: ""
    property color summaryColor: palette.placeholderText
    // 卡片内容：default 子项落到 body 列（列间距统一 12）
    default property alias content: body.data

    padding: collapsible && !expanded ? UiMetrics.space2
             : compact ? UiMetrics.space3 : UiMetrics.space5

    background: Rectangle {
        color: card.palette.button
        radius: UiMetrics.radiusM
        border.width: 1
        border.color: Qt.rgba(card.palette.windowText.r, card.palette.windowText.g,
                             card.palette.windowText.b, 0.08)
    }

    contentItem: ColumnLayout {
        spacing: card.compact ? UiMetrics.space1 : UiMetrics.space2

        ItemDelegate {
            objectName: "cardExpander"
            visible: card.collapsible
            Layout.fillWidth: true
            implicitHeight: 32
            text: card.title
            Accessible.checkable: true
            Accessible.checked: card.expanded
            onClicked: card.expanded = !card.expanded
            contentItem: RowLayout {
                spacing: UiMetrics.space2
                Text {
                    text: card.title
                    color: card.palette.windowText
                    font.pixelSize: UiMetrics.fontM
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Text {
                    text: card.summary
                    visible: text.length > 0
                    color: card.summaryColor
                    font.pixelSize: UiMetrics.fontS
                    elide: Text.ElideRight
                    Layout.maximumWidth: card.availableWidth * 0.45
                    Layout.minimumWidth: 0
                }
                Text {
                    text: card.expanded ? "\uE70E" : "\uE70D"
                    font.family: "Segoe MDL2 Assets"
                    font.pixelSize: 10
                    color: card.palette.placeholderText
                }
            }
        }
        Text {
            visible: !card.collapsible && card.title.length > 0
            text: card.title
            color: card.palette.windowText
            font.pixelSize: UiMetrics.fontL
            font.weight: Font.DemiBold
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Text {
            visible: (!card.collapsible || card.expanded) && card.description.length > 0
            text: card.description
            color: card.palette.placeholderText
            font.pixelSize: UiMetrics.fontS
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ColumnLayout {
            id: body
            // 只隐藏内容，不卸载编辑器：折叠不能丢草稿或清除非法输入计数。
            visible: !card.collapsible || card.expanded
            spacing: card.compact ? UiMetrics.space2 : UiMetrics.space3
            Layout.fillWidth: true
            Layout.topMargin: card.title.length > 0
                              ? (card.compact ? UiMetrics.space1 : UiMetrics.space2) : 0
        }
    }
}
