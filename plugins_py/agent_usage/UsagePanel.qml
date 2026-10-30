// UsagePanel.qml — 可滚动 Agent 用量面板，统一显示统计口径、未知值和额度时效
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "TokenFormat.js" as TokenFormat

Rectangle {
    id: panel
    property var plugin
    property var pluginContext
    property var values: ({})
    property var colors: ({"bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#9399b2",
        "border": "#45475a", "accent": "#89b4fa", "warning": "#f9e2af", "danger": "#f38ba8", "success": "#a6e3a1"})
    signal refreshRequested()
    property var selectedAgent: null
    onValuesChanged: {
        // 隐藏正在查看的工具后返回列表，避免详情继续展示已取消勾选的来源。
        if (selectedAgent && (values.hiddenAgentIds || []).indexOf(selectedAgent.id) >= 0)
            selectedAgent = null
    }
    width: 620; height: 660
    color: colors.bg; radius: 12; border.color: colors.border
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function openAgent(agent) { selectedAgent = ({ id: agent.id, name: agent.name, scope: agent.scope }) }
    function number(n) { return TokenFormat.compact(n) }
    function cost(n) { return "$" + Number(n || 0).toFixed(2) }
    function periodName() {
        switch (values.period) {
        case "7d": return tr("近 7 天")
        case "30d": return tr("近 30 天")
        case "all": return tr("全部历史")
        default: return tr("今天")
        }
    }
    function statusText(status) {
        switch (status) {
        case "loading": return tr("正在采集，首次扫描可能较慢")
        case "python_missing": return tr("Python 采集进程不可用，请检查插件依赖")
        case "timeout": return tr("采集超时，保留上次结果")
        case "collector_failed": return tr("采集失败，请检查插件依赖与登录状态")
        case "sync_busy": return tr("另一个采集任务正在运行，稍后重试")
        case "proxy_failed": return tr("代理配置不可用，请检查插件网络设置")
        case "disabled": return tr("套餐额度采集已关闭")
        case "reauth": return tr("请在对应 Agent 中重新登录")
        case "unavailable": return tr("暂时无法获取额度")
        case "not_configured": return tr("未检测到登录状态")
        case "no_quota": return tr("此账号未返回额度窗口")
        case "unsupported": return tr("此工具暂无独立额度接口")
        case "pending": return tr("额度尚未获取")
        case "cli_missing": return tr("未找到该工具的原生额度客户端")
        default: return ""
        }
    }
    function windowName(w) {
        var prefix = w.id.indexOf("spark") >= 0 ? "Spark · " : ""
        if (w.seconds > 0) return prefix + (w.seconds >= 86400 ? tr("%1 天窗口").arg(Math.round(w.seconds / 86400)) : tr("%1 小时窗口").arg((w.seconds / 3600).toFixed(0)))
        if (w.id === "credit_window") return tr("额度")
        return prefix + ({"primary_window": tr("主窗口"), "secondary_window": tr("第二窗口"),
            "tertiary_window": tr("第三窗口"), "quaternary_window": tr("第四窗口")}[w.id] || tr("额度窗口"))
    }
    function resetText(value) {
        if (!value) return tr("重置时间未知")
        var date = new Date(value)
        if (isNaN(date.getTime())) return tr("重置时间未知")
        return tr("重置于 %1").arg(Qt.formatDateTime(date, "MM-dd HH:mm"))
    }

    ColumnLayout {
        visible: !panel.selectedAgent
        anchors.fill: parent; anchors.margins: 18; spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: panel.tr("Agent 用量"); color: panel.colors.fg; font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
            Rectangle {
                width: 92; height: 30; radius: 6; color: panel.colors.panel
                Text { anchors.centerIn: parent; text: panel.tr("刷新"); color: panel.colors.accent; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.refreshRequested() }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: panel.periodName() + " · " + (panel.values.includeAccount ? panel.tr("含账号级来源") : panel.tr("仅本机来源汇总"))
            color: panel.colors.muted; font.pixelSize: 12
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 72; radius: 8; color: panel.colors.panel
            RowLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 3
                    Text { text: panel.values.usageUpdated ? panel.number(panel.values.totalTokens) : "—"; font.pixelSize: 23; font.bold: true; color: panel.colors.fg }
                    Text { text: "Token"; font.pixelSize: 11; color: panel.colors.muted }
                }
                ColumnLayout {
                    spacing: 3
                    Text { text: panel.values.usageUpdated ? panel.cost(panel.values.totalCost) + (panel.values.unpriced ? " + ?" : "") : "—"; font.pixelSize: 21; color: panel.colors.accent }
                    Text { text: panel.tr("费用估算 · USD"); font.pixelSize: 11; color: panel.colors.muted }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: panel.colors.warning
            visible: text.length > 0
            text: [panel.statusText(panel.values.usageStatus), panel.statusText(panel.values.quotaStatus), panel.values.sourceErrorCount > 0 ? panel.tr("部分来源采集失败，保留上次记录") : ""].filter(function(s, i, a) { return s && a.indexOf(s) === i }).join(" · ")
        }
        TextField {
            id: search; Layout.fillWidth: true; implicitHeight: 32
            placeholderText: panel.tr("筛选 Agent…"); font.pixelSize: 12
            color: panel.colors.fg; placeholderTextColor: panel.colors.muted; selectionColor: panel.colors.accent
            background: Rectangle { color: panel.colors.panel; radius: 6; border.color: panel.colors.border }
        }
        Text {
            objectName: "agentFilterHint"
            Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!panel.values.popupFiltered
            text: panel.tr("列表已按设置筛选；上方总量仍按全部来源汇总。")
            font.pixelSize: 11; color: panel.colors.muted
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                width: 6; policy: ScrollBar.AsNeeded
                contentItem: Rectangle { implicitWidth: 6; radius: 3; color: panel.colors.muted; opacity: 0.6 }
                background: Item {}
            }
            Column {
                width: parent.width; spacing: 8
                Repeater {
                    model: panel.values.agents || []
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property var stats: modelData.stats
                        readonly property var quota: modelData.quota
                        width: parent.width
                        visible: modelData.name.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                        height: visible ? details.implicitHeight + 22 : 0
                        radius: 8; color: panel.colors.panel
                        border.color: cardMouse.containsMouse ? panel.colors.accent : "transparent"
                        ColumnLayout {
                            id: details; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 11; spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: card.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; font.bold: true; font.pixelSize: 14; color: panel.colors.fg }
                                Text { text: card.stats ? panel.number(card.stats.total_tokens) + " tokens" : panel.tr("本周期无记录"); font.pixelSize: 12; color: panel.colors.fg }
                                Text { text: "›"; font.pixelSize: 18; color: panel.colors.accent }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!card.stats; color: panel.colors.muted; font.pixelSize: 11
                                text: card.stats ? panel.tr("输入 %1 · 输出 %2 · 缓存读取 %3 · 缓存写入 %4").arg(panel.number(card.stats.input_tokens)).arg(panel.number(card.stats.output_tokens)).arg(panel.number(card.stats.cached_input_tokens)).arg(panel.number(card.stats.cache_creation_input_tokens)) : ""
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!card.stats; color: panel.colors.muted; font.pixelSize: 11
                                text: card.stats ? panel.tr("估算 %1").arg(panel.cost(card.stats.cost) + (card.stats.unpriced ? " + ?" : "")) + " · " + (card.modelData.scope === "account" ? panel.tr("账号级数据，可能包含其他设备") : panel.tr("本机记录")) : ""
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!card.modelData.sourceError || !!(card.stats && card.stats.estimated)
                                text: card.modelData.sourceError ? panel.tr("此来源暂时无法读取，显示已有记录") : panel.tr("部分 Token 根据工具日志估算")
                                color: panel.colors.warning; font.pixelSize: 11
                            }
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; visible: !!card.quota.plan; text: card.quota.plan || ""; color: panel.colors.accent; font.pixelSize: 11 }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: text.length > 0; color: panel.colors.muted; font.pixelSize: 11
                                text: panel.values.quotaStatus === "disabled" ? panel.statusText("disabled") : panel.statusText(card.quota.status)
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!card.quota.stale; color: panel.colors.warning; font.pixelSize: 11
                                text: panel.tr("上次额度缓存 · %1").arg(card.quota.capturedAt ? Qt.formatDateTime(new Date(card.quota.capturedAt), "MM-dd HH:mm") : "—")
                            }
                            Repeater {
                                model: panel.values.quotaStatus === "disabled" ? [] : (card.quota.windows || [])
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true; spacing: 3
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: panel.windowName(modelData); color: panel.colors.fg; font.pixelSize: 11; Layout.fillWidth: true }
                                        Text { text: panel.tr("已用 %1%").arg(Number(modelData.used).toFixed(1)); color: modelData.used >= 90 ? panel.colors.danger : panel.colors.fg; font.pixelSize: 11 }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 4; radius: 2; color: panel.colors.border
                                        Rectangle { height: parent.height; radius: 2; width: parent.width * Math.min(100, modelData.used) / 100; color: modelData.used >= 90 ? panel.colors.danger : panel.colors.accent }
                                    }
                                    Text { text: panel.resetText(modelData.resetAt); color: panel.colors.muted; font.pixelSize: 10 }
                                }
                            }
                        }
                        MouseArea {
                            id: cardMouse; objectName: "agentCard_" + card.modelData.id
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: panel.openAgent(card.modelData)
                            ToolTip.visible: containsMouse
                            ToolTip.text: panel.tr("点击查看指定时间段的用量明细")
                        }
                    }
                }
                Text {
                    objectName: "agentEmptyState"
                    width: parent.width; wrapMode: Text.Wrap
                    visible: !(panel.values.agents || []).length
                    text: panel.values.popupAgentMode === "active"
                        ? panel.tr("本统计周期尚无 Token 消耗记录。")
                        : panel.values.popupFiltered
                        ? panel.tr("所选工具暂无可显示的统计。请在插件设置中调整选择，或启用「列出无记录的工具」。")
                        : panel.tr("尚无可显示的用量。采集完成后会自动列出有记录或已登录的工具。")
                    font.pixelSize: 12; color: panel.colors.muted
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: panel.colors.muted; font.pixelSize: 10
            text: panel.tr("Token 更新：%1 · 额度更新：%2").arg(panel.values.usageUpdated || "—").arg(panel.values.quotaUpdated || "—")
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: panel.colors.muted; font.pixelSize: 10
            text: panel.tr("费用是按模型价格计算的估算值，不代表订阅账单；? 表示部分模型缺少价格。")
        }
    }
    Loader {
        anchors.fill: parent; active: !!panel.selectedAgent
        sourceComponent: Component {
            AgentDetail {
                plugin: panel.plugin; pluginContext: panel.pluginContext
                values: panel.values; colors: panel.colors; agent: panel.selectedAgent
                translate: panel.tr; sharedStatusText: panel.statusText
                onBackRequested: panel.selectedAgent = null
            }
        }
    }
}
