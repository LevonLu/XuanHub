// UiSettingRow.qml — WinUI 设置卡片：图标、说明与右侧操作，窄屏时操作换行
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Pane {
    id: card
    property string title: ""
    property string description: ""
    property string glyph: ""
    readonly property bool wide: width >= 620
    default property alias content: editor.data
    padding: UiMetrics.space4

    background: Rectangle {
        color: card.palette.button
        radius: UiMetrics.radiusM
        border.width: 1
        border.color: Qt.rgba(card.palette.windowText.r, card.palette.windowText.g,
                             card.palette.windowText.b, 0.08)
    }
    contentItem: GridLayout {
        columns: card.wide ? 3 : 2
        columnSpacing: UiMetrics.space4
        rowSpacing: UiMetrics.space3

        Text {
            text: card.glyph
            color: card.palette.windowText
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 20
            Layout.preferredWidth: 24
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.space1
            Label {
                text: card.title
                font.pixelSize: UiMetrics.fontM
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: card.description
                color: card.palette.placeholderText
                font.pixelSize: UiMetrics.fontS
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        RowLayout {
            id: editor
            Layout.column: card.wide ? 2 : 1
            Layout.row: card.wide ? 0 : 1
            Layout.preferredWidth: 224
            Layout.fillWidth: !card.wide
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: UiMetrics.space2
        }
    }
}
