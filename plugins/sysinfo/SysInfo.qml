// SysInfo.qml — 系统信息多指标显示（Grid 布局，按列填充）
// 范式：根 Item 声明 property var plugin（ViewModel，即 QAbstractListModel）；
// Grid 的 rows = itemsPerColumn + flow = TopToBottom，使指标先填满一列再向右另起一列。
// 每项文本 = prefix + text（text 由 ViewModel 按 metric 路由，随采集刷新）；
// 颜色：item.color 非空用配置色，空串则按 metric 回落主题语义色。
import QtQuick

Item {
    id: root
    property var plugin
    property var pluginContext

    // 宽度/高度跟随内容（列数与前缀长度不定），两侧留呼吸位
    width: grid.implicitWidth + 12
    height: grid.implicitHeight + 6

    // metric → 主题语义色的映射（color 为空串时回落用）
    function themeColor(metric) {
        if (!themeManager) {
            // 无主题时的硬编码兜底（与 netstat/sysinfo 旧版一致）
            if (metric === "cpu")     return "#89b4fa"   // accent
            if (metric === "mem")     return "#a6e3a1"   // success
            if (metric === "gpu")     return "#f9e2af"   // warning
            if (metric === "netDown") return "#a6e3a1"   // success
            if (metric === "netUp")   return "#f38ba8"   // danger
            return "#cdd6f4"
        }
        const c = themeManager.colors
        if (metric === "cpu")     return c.accent
        if (metric === "mem")     return c.success
        if (metric === "gpu")     return c.warning
        if (metric === "netDown") return c.success
        if (metric === "netUp")   return c.danger
        return c.fg
    }

    Grid {
        id: grid
        anchors.centerIn: parent
        spacing: 6
        // 按列填充：rows 决定每列项数，超出向右另起一列（flow=TopToBottom）
        rows: plugin ? plugin.itemsPerColumn : 1
        flow: Grid.TopToBottom
        // 列宽随最宽项自适应（不强制等宽，保留自然间距）
        columnSpacing: spacing
        rowSpacing: spacing

        Repeater {
            model: plugin   // SysInfoViewModel（QAbstractListModel）

            Text {
                text: (model.prefix || "") + (model.text || "---")
                color: (model.color && model.color !== "")
                       ? model.color
                       : root.themeColor(model.metric)
                font.pixelSize: 12
            }
        }
    }
}
