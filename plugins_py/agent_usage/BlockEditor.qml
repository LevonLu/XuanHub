// BlockEditor.qml — 单个显示块的指标、来源、窗口与标签编辑
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "SettingsData.js" as Data

ColumnLayout {
    id: root
    property var blockSpec: ({metric: "empty", targetId: ""})
    property var accounts: []
    property int accountRevision: 0
    signal changed(var next)
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function change(key, value) {
        var next = JSON.parse(JSON.stringify(blockSpec))
        next[key] = value
        if (key === "metric" && value === "tokens" && !Data.sources.some(function(s) { return s.value === next.targetId })) next.targetId = "total"
        root.blockSpec = next
        changed(next)
    }
    readonly property var targets: {
        var revision=accountRevision
        if (blockSpec.metric === "tokens") return Data.sources
        var monetary = ["balance", "spendToday"].indexOf(blockSpec.metric) >= 0
        return (monetary ? [] : Data.autoAccounts).concat(accounts.filter(function(a) {
            return monetary ? a.providerId === "deepseek" : a.providerId !== "deepseek"
        }).map(function(a) { return {value:a.id,label:a.name || a.providerId} }))
    }
    function windowOptions() {
        var target = root.blockSpec.targetId
        var account = root.accounts.find(function(a) { return a.id === target })
        if (target === "auto:codex-spark") return [{value:"spark_primary_window",label:tr("主窗口")},{value:"spark_secondary_window",label:tr("第二窗口")}]
        if (target === "auto:opencode-go" || (account && account.providerId === "opencode-go")) return [{value:"primary_window",label:tr("rolling")},{value:"secondary_window",label:tr("weekly")},{value:"tertiary_window",label:tr("monthly")}]
        return Data.windows.map(function(o) { return {value:o.value,label:tr(o.label)} })
    }
    SettingField {
        label: root.tr("显示内容"); kind: "enum"; value: root.blockSpec.metric
        options: Data.metrics.map(function(o) { return {value:o.value,label:root.tr(o.label)} })
        onEdited: function(v) { root.change("metric", v) }
    }
    SettingField {
        visible: root.blockSpec.metric !== "empty"
        label: root.tr("数据来源"); kind: "enum"; value: root.blockSpec.targetId
        options: [{value:"",label:root.tr("请选择")}].concat(root.targets.map(function(o) { return {value:o.value,label:root.tr(o.label)} }))
        onEdited: function(v) { root.change("targetId", v) }
    }
    SettingField {
        visible: ["window", "reset"].indexOf(root.blockSpec.metric) >= 0
        label: root.tr("额度窗口"); kind: "enum"; value: root.blockSpec.windowId || "primary_window"
        options: root.windowOptions()
        onEdited: function(v) { root.change("windowId", v) }
    }
    SettingField {
        visible: ["balance", "spendToday"].indexOf(root.blockSpec.metric) >= 0
        label: root.tr("币种"); kind: "enum"; value: root.blockSpec.currency || ""
        options: [{value:"",label:root.tr("账户默认币种")},{value:"CNY",label:"CNY"},{value:"USD",label:"USD"}]
        onEdited: function(v) { root.change("currency", v) }
    }
    SettingField {
        visible: root.blockSpec.metric !== "empty"
        label: root.tr("自定义标签"); value: root.blockSpec.label || ""
        onEdited: function(v) { root.change("label", v) }
    }
}
