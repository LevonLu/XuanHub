// BarQuotaSummary.qml — 总用量与单、双时段套餐限额的两行布局
import QtQuick

Item {
    id: root
    property var entries: []
    property string usageText: ""
    property var colors: ({"fg": "#cdd6f4", "warning": "#f9e2af", "danger": "#f38ba8"})
    readonly property bool hasBothPeriods: entries.some(function(e) { return e.windows.some(function(w) { return w.period === "5h" }) })
        && entries.some(function(e) { return e.windows.some(function(w) { return w.period === "7d" }) })
    readonly property bool stackUsage: usageText.length > 0 && entries.length > 0 && !hasBothPeriods
    implicitWidth: stackUsage ? Math.max(total.width, content.implicitWidth)
        : total.width + (total.visible && entries.length ? 12 : 0) + content.implicitWidth
    implicitHeight: stackUsage ? total.implicitHeight + 2 + content.implicitHeight
        : Math.max(total.visible ? total.implicitHeight : 0, content.implicitHeight)

    function windowText(entry, window) {
        var used = window.used === null || window.used === undefined
            ? "—" : Number(window.used).toFixed(1).replace(/\.0$/, "") + "%"
        return entry.name + " " + window.period + " " + used + (entry.stale ? " *" : "")
    }
    function windowColor(entry, window) {
        if (entry.stale) return colors.warning
        if (window.used !== null && window.used >= 90) return colors.danger
        return colors.fg
    }
    Text {
        id: total
        objectName: "agentUsageLabel"
        visible: root.usageText.length > 0
        width: Math.min(212, implicitWidth)
        y: root.stackUsage ? 0 : (root.height - height) / 2
        text: root.usageText
        textFormat: Text.PlainText
        elide: Text.ElideRight
        color: root.colors.fg
        font.pixelSize: 12
    }
    Row {
        id: content
        objectName: "agentBarQuotaRows"
        x: root.stackUsage || !total.visible ? 0 : total.width + 12
        y: root.stackUsage ? total.implicitHeight + 2 : (root.height - height) / 2
        spacing: 12
        Repeater {
            model: root.entries
            delegate: Column {
                id: entry
                required property var modelData
                objectName: "barQuota_" + modelData.id
                Repeater {
                    // 混合选择也固定 5h 在上、7d 在下，空白行保留对齐。
                    model: root.hasBothPeriods ? ["5h", "7d"] : entry.modelData.windows.map(function(w) { return w.period })
                    delegate: Text {
                        required property string modelData
                        readonly property var window: entry.modelData.windows.find(function(w) { return w.period === modelData })
                        objectName: "barQuota_" + entry.modelData.id + "_" + modelData
                        text: window ? root.windowText(entry.modelData, window) : ""
                        // Column 会跳过零宽子项；空白占位也须保留时段对应的行高。
                        width: Math.max(1, implicitWidth)
                        height: Math.max(14, implicitHeight)
                        textFormat: Text.PlainText
                        color: window ? root.windowColor(entry.modelData, window) : root.colors.fg
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
