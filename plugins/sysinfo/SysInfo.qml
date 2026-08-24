// SysInfo.qml — 状态栏系统指标入口与详情弹窗
import QtQuick
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    // 由 PluginSlot 传入：已经扣除了胶囊区域和单项上下内边距的实际可用高度。
    // 未经宿主加载时回退 parent/grid，便于独立预览。
    property real hostContentHeight: 0
    readonly property real availableContentHeight: hostContentHeight > 0
                                                ? hostContentHeight
                                                : (parent && parent.height > 0
                                                   ? parent.height : grid.implicitHeight)
    // 指标内容高于可用空间时等比缩放文字、图标和行间距，保证多行布局完整可见。
    readonly property real contentScale: grid.implicitHeight > 0
                                         ? Math.min(1, availableContentHeight / grid.implicitHeight)
                                         : 1

    width: grid.implicitWidth + 16
    height: availableContentHeight

    function colors() {
        if (pluginContext && pluginContext.colors) return pluginContext.colors
        if (typeof themeManager !== "undefined" && themeManager) return themeManager.colors
        return {
            bg: "#1e1e2e", panel: "#313244", fg: "#cdd6f4", muted: "#7f849c",
            accent: "#89b4fa", success: "#a6e3a1", warning: "#f9e2af",
            danger: "#f38ba8", hover: "#45475a", border: "#45475a"
        }
    }

    function metricColor(metric) {
        const c = colors()
        if (metric === "cpu") return c.accent
        if (metric === "mem") return c.success
        if (metric === "gpu") return c.warning
        if (metric === "netDown") return c.success
        if (metric === "netUp") return c.danger
        return c.fg
    }

    Grid {
        id: grid
        anchors.centerIn: parent
        transformOrigin: Item.Center
        scale: root.contentScale
        rows: plugin ? plugin.itemsPerColumn : 1
        flow: Grid.TopToBottom
        columnSpacing: 10
        rowSpacing: 5

        Repeater {
            model: plugin

            Row {
                spacing: 4

                SysInfoIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    metric: model.metric
                    iconColor: (model.color && model.color !== "")
                               ? model.color : root.metricColor(model.metric)
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (model.prefix || "") + (model.text || "---")
                    color: (model.color && model.color !== "")
                           ? model.color : root.metricColor(model.metric)
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: detailsPopup.toggleFromBar()
    }

    PluginPopup {
        id: detailsPopup
        anchorItem: root
        pluginContext: root.pluginContext
        width: {
            const geometry = root.pluginContext ? root.pluginContext.screenGeometry : null
            return geometry && geometry.width > 0 ? Math.min(760, geometry.width - 24) : 760
        }
        height: 590

        SysInfoDetails {
            anchors.fill: parent
            plugin: root.plugin
            pluginContext: root.pluginContext
            onCloseRequested: detailsPopup.closePopup()
        }
    }
}
