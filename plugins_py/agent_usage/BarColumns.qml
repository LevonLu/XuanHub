// BarColumns.qml — 单块独占或上下双块的稳定列布局
import QtQuick
import "BlockFormat.js" as Format

Row {
    id: root
    property var columns: []
    property var colors: ({fg: "#ffffff", warning: "#fce100", danger: "#ff99a4"})
    property double now: Date.now()
    spacing: 12
    Timer { interval: 1000; running: true; repeat: true; onTriggered: root.now = Date.now() }
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function ink(block) {
        if (block.stale || block.affected) return colors.warning
        if (["quota5h", "quota7d", "window"].indexOf(block.metric) >= 0 && block.value !== null && block.value >= 90) return colors.danger
        return colors.fg
    }
    Repeater {
        model: root.columns
        delegate: Item {
            id: col
            required property var modelData
            readonly property bool single: modelData.layout === "single"
            width: Math.max(4, single ? whole.implicitWidth : Math.max(upper.implicitWidth, lower.implicitWidth))
            height: 30
            Text {
                id: whole; visible: col.single; anchors.verticalCenter: parent.verticalCenter
                text: Format.text(col.modelData.single, root.tr, root.now); textFormat: Text.PlainText
                font.pixelSize: 12; color: root.ink(col.modelData.single)
            }
            Text {
                id: upper; visible: !col.single; y: 0
                text: Format.text(col.modelData.top, root.tr, root.now); textFormat: Text.PlainText
                font.pixelSize: 12; color: root.ink(col.modelData.top)
            }
            Text {
                id: lower; visible: !col.single; y: 16
                text: Format.text(col.modelData.bottom, root.tr, root.now); textFormat: Text.PlainText
                font.pixelSize: 12; color: root.ink(col.modelData.bottom)
            }
        }
    }
}
