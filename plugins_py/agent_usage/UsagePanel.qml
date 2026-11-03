// UsagePanel.qml — 紧凑用量总览、套餐额度和按内容收缩的弹窗面板
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "TokenFormat.js" as TokenFormat

Rectangle {
    id: panel
    objectName: "agentUsagePanel"
    property var plugin
    property var pluginContext
    property var values: ({})
    property var colors: ({"bg": "#202020", "panel": "#2b2b2b", "fg": "#ffffff", "muted": "#cecece",
        "border": "#484848", "accent": "#60cdff", "warning": "#fce100", "danger": "#ff99a4", "success": "#6ccb5f"})
    property var selectedAgent: null
    property bool showHeader: true
    property bool framed: true
    signal refreshRequested()
    onValuesChanged: {
        // 设置隐藏当前工具时返回总览，防止详情脱离用户的显示范围。
        if (selectedAgent && (values.hiddenAgentIds || []).indexOf(selectedAgent.id) >= 0)
            selectedAgent = null
    }
    width: 420
    height: Math.min(620, implicitHeight)
    // 列表使用自然内容高度；不能从可视高度反推，避免与宿主的限高形成循环。
    implicitHeight: selectedAgent ? 570 : overview.implicitHeight + 40
    color: framed ? colors.bg : "transparent"; radius: 16
    border.color: framed ? Qt.alpha(colors.border, 0.8) : "transparent"

    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function openAgent(agent) { selectedAgent = ({id: agent.id, name: agent.name, scope: agent.scope}) }
    function number(n) { return TokenFormat.compact(n) }
    function shortTime(value) {
        var match = String(value || "").match(/\d{2}:\d{2}/)
        return match ? match[0] : "—"
    }
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
        case "loading": return values.usageUpdated ? tr("正在更新…") : tr("首次采集中…")
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
        return isNaN(date.getTime()) ? tr("重置时间未知") : tr("重置于 %1").arg(Qt.formatDateTime(date, "MM-dd HH:mm"))
    }

    ColumnLayout {
        id: overview
        visible: !panel.selectedAgent
        anchors.fill: parent; anchors.margins: 20; spacing: 16
        RowLayout {
            Layout.fillWidth: true; visible: panel.showHeader
            Text { text: panel.tr("Token Usage"); color: panel.colors.fg; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            PopupAction {
                objectName: "agentRefreshButton"
                text: panel.tr("刷新"); iconText: "↻"; colors: panel.colors; quiet: true
                onClicked: panel.refreshRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: hero.implicitHeight + 32
            radius: 12; color: Qt.alpha(panel.colors.accent, 0.055)
            ColumnLayout {
                id: hero
                anchors.fill: parent; anchors.margins: 16; spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: panel.tr("Token 总用量"); color: panel.colors.muted; font.pixelSize: 11; Layout.fillWidth: true }
                    Rectangle {
                        implicitWidth: periodLabel.implicitWidth + 16; implicitHeight: 23; radius: 6
                        color: Qt.alpha(panel.colors.accent, 0.08)
                        Text { id: periodLabel; anchors.centerIn: parent; text: panel.periodName(); color: panel.colors.accent; font.pixelSize: 10; font.weight: Font.Medium }
                    }
                }
                Text {
                    objectName: "agentOverviewTotal"
                    Layout.fillWidth: true
                    text: panel.values.usageUpdated ? panel.number(panel.values.totalTokens) : "—"
                    color: panel.colors.fg; font.pixelSize: 36; font.weight: Font.DemiBold
                    font.letterSpacing: -1; elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { implicitWidth: 5; implicitHeight: 5; radius: 3; color: panel.colors.muted; opacity: 0.7 }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: panel.values.includeAccount ? panel.tr("含账号级来源") : panel.tr("仅本机来源汇总")
                        color: panel.colors.muted; font.pixelSize: 10
                    }
                }
            }
        }
        Text {
            objectName: "agentCollectionStatus"
            Layout.fillWidth: true; wrapMode: Text.Wrap; visible: text.length > 0
            font.pixelSize: 11; color: panel.colors.warning
            text: [panel.statusText(panel.values.usageStatus), panel.statusText(panel.values.quotaStatus),
                panel.values.sourceErrorCount > 0 ? panel.tr("部分来源采集失败，保留上次记录") : ""]
                .filter(function(s, i, a) { return s && a.indexOf(s) === i }).join(" · ")
        }
        Flickable {
            id: agentsView
            objectName: "agentListViewport"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            implicitHeight: cards.implicitHeight
            contentWidth: width; contentHeight: cards.implicitHeight
            clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                width: 4; policy: ScrollBar.AsNeeded
                contentItem: Rectangle { implicitWidth: 4; radius: 2; color: panel.colors.muted; opacity: 0.5 }
                background: Item {}
            }
            Column {
                id: cards
                width: agentsView.width - (agentsView.contentHeight > agentsView.height ? 8 : 0); spacing: 10
                Repeater {
                    model: panel.values.agents || []
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property var stats: modelData.stats
                        readonly property var quota: modelData.quota || ({status:"pending",windows:[]})
                        width: cards.width; height: details.implicitHeight + 32
                        radius: 12
                        color: cardMouse.containsMouse ? Qt.alpha(panel.colors.accent, 0.055) : Qt.alpha(panel.colors.fg, 0.025)
                        border.color: cardMouse.containsMouse ? Qt.alpha(panel.colors.accent, 0.4) : Qt.alpha(panel.colors.border, 0.65)
                        Behavior on color { ColorAnimation { duration: 120 } }
                        ColumnLayout {
                            id: details
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 16; spacing: 12
                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Rectangle {
                                    implicitWidth: 32; implicitHeight: 32; radius: 10
                                    color: Qt.alpha(panel.colors.accent, 0.09)
                                    Text {
                                        anchors.centerIn: parent; text: String(card.modelData.name || "?").charAt(0).toUpperCase()
                                        color: panel.colors.accent; font.pixelSize: 14; font.weight: Font.DemiBold
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 3
                                    Text {
                                        Layout.fillWidth: true; text: card.modelData.name; textFormat: Text.PlainText
                                        elide: Text.ElideRight; color: panel.colors.fg; font.weight: Font.DemiBold; font.pixelSize: 13
                                    }
                                    Text {
                                        Layout.fillWidth: true; visible: text.length > 0; elide: Text.ElideRight
                                        text: [card.quota.plan || "", card.modelData.scope === "account" ? panel.tr("账号级数据") : ""].filter(function(s) { return s }).join(" · ")
                                        textFormat: Text.PlainText; color: panel.colors.muted; font.pixelSize: 10
                                    }
                                }
                                Text {
                                    text: card.stats ? panel.number(card.stats.total_tokens) : "—"
                                    color: panel.colors.fg; font.pixelSize: 19; font.weight: Font.DemiBold
                                }
                                Text { text: "›"; color: panel.colors.muted; font.pixelSize: 19 }
                            }
                            Repeater {
                                model: panel.values.quotaStatus === "disabled" ? [] : (card.quota.windows || [])
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true; spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: panel.windowName(modelData); color: panel.colors.muted; font.pixelSize: 10; Layout.fillWidth: true }
                                        Text { text: Number(modelData.used).toFixed(1) + "%"; color: modelData.used >= 90 ? panel.colors.danger : panel.colors.accent; font.pixelSize: 12; font.bold: true }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 5; radius: 3; color: Qt.alpha(panel.colors.accent, 0.12)
                                        Rectangle {
                                            height: parent.height; radius: 3; width: parent.width * Math.max(0, Math.min(100, modelData.used)) / 100
                                            color: modelData.used >= 90 ? panel.colors.danger : panel.colors.accent
                                        }
                                    }
                                    Text { text: panel.resetText(modelData.resetAt); color: panel.colors.muted; font.pixelSize: 10 }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                visible: panel.values.quotaStatus !== "disabled" && text.length > 0
                                text: panel.statusText(card.quota.status)
                                color: panel.colors.muted; font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap; visible: !!card.quota.stale
                                text: panel.tr("上次额度缓存 · %1").arg(panel.shortTime(card.quota.capturedAt))
                                color: panel.colors.warning; font.pixelSize: 10
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                visible: !!card.modelData.sourceError || !!(card.stats && card.stats.estimated)
                                text: card.modelData.sourceError ? panel.tr("此来源暂时无法读取，显示已有记录") : panel.tr("部分 Token 根据工具日志估算")
                                color: panel.colors.warning; font.pixelSize: 10
                            }
                        }
                        MouseArea {
                            id: cardMouse; objectName: "agentCard_" + card.modelData.id
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: panel.openAgent(card.modelData)
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
                    color: panel.colors.muted; font.pixelSize: 12
                }
            }
        }
        Text {
            objectName: "agentUpdateTimes"
            Layout.fillWidth: true; wrapMode: Text.Wrap
            visible: !!panel.values.usageUpdated || !!panel.values.quotaUpdated
            text: panel.tr("Token 更新：%1 · 额度更新：%2").arg(panel.shortTime(panel.values.usageUpdated)).arg(panel.shortTime(panel.values.quotaUpdated))
            color: panel.colors.muted; font.pixelSize: 10
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
