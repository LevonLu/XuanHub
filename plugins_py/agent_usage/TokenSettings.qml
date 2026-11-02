// TokenSettings.qml — 列布局、账户管理与原有统计设置的统一草稿页
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub
import "SettingsData.js" as Data

Item {
    id: root
    property var plugin
    property var columns: []
    property var accounts: []
    property int section: 0
    property int previewRevision: 0
    implicitHeight: content.implicitHeight
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function validProxy() { return plugin && plugin.pluginId === "py_agent_usage" }
    function v(key, fallback) { var revision = plugin ? plugin.revision : 0; var value = validProxy() ? plugin.value(key) : undefined; return value === undefined ? fallback : value }
    function emptyBlock() { return {metric:"empty",targetId:""} }
    function commitColumns(structural) { if (!validProxy()) return; if (structural) columns = columns.slice(); else previewRevision++; plugin.setValue("barColumns", JSON.parse(JSON.stringify(columns))) }
    function commitAccounts(structural) { if (!validProxy()) return; if (structural) accounts = accounts.slice(); previewRevision++; plugin.setValue("accounts", JSON.parse(JSON.stringify(accounts))) }
    function editAccount(index, key, value) { accounts[index][key] = value; commitAccounts(key === "providerId") }
    function moveColumn(index, delta) { var next = index + delta; if (next < 0 || next >= columns.length) return; var temp = columns[index]; columns[index] = columns[next]; columns[next] = temp; commitColumns(true) }
    onPluginChanged: {
        if (!validProxy()) return
        columns = JSON.parse(JSON.stringify(v("barColumns", Data.defaultColumns(function(key, fallback) { return root.v(key, fallback) }))))
        accounts = JSON.parse(JSON.stringify(v("accounts", [])))
    }
    readonly property var preview: { var revision = previewRevision; return columns.map(function(c) {
        var next = JSON.parse(JSON.stringify(c))
        for (var slot of ["single","top","bottom"]) {
            var b = next[slot] || root.emptyBlock(); next[slot] = b
            var target = Data.sources.concat(Data.autoAccounts).find(function(o) { return o.value === b.targetId })
            var account = root.accounts.find(function(a) { return a.id === b.targetId })
            b.name = target ? root.tr(target.label) : account ? account.name : ""
            b.value = b.metric === "tokens" ? 3670000 : b.metric === "balance" ? "56.80" : b.metric === "spendToday" ? "2.30" : 42
            b.currency = b.currency || "CNY"; b.resetAt = new Date(Date.now() + 7200000).toISOString()
        }
        return next
    }) }
    ColumnLayout {
        id: content; width: parent.width; spacing: 12
        Label { id: titleControl; text: "Token Usage"; font.pixelSize: 18; font.bold: true }
        Label { visible: !!root.v("legacyImportPending", false); text: root.tr("旧 Usage Hub 已合并，应用此页后安全保存迁移。"); wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button {
            visible: !!root.v("legacyImportPending", false)
            text: root.tr("采用合并配置")
            onClicked: if (root.validProxy()) root.plugin.setValue("legacyImportPending", false)
        }
        RowLayout {
            Button { text: root.tr("状态栏布局"); highlighted: root.section === 0; onClicked: root.section = 0 }
            Button { text: root.tr("账户管理"); highlighted: root.section === 1; onClicked: root.section = 1 }
            Button { text: root.tr("统计与网络"); highlighted: root.section === 2; onClicked: root.section = 2 }
        }
        ColumnLayout {
            visible: root.section === 0; Layout.fillWidth: true; spacing: 12
            Label { text: root.tr("示例预览（非实时数据）"); color: palette.placeholderText }
            Flickable {
                Layout.fillWidth: true; implicitHeight: 46; contentWidth: previewBar.width; contentHeight: height; clip: true
                BarColumns { id: previewBar; columns: root.preview; anchors.verticalCenter: parent.verticalCenter; colors: ({fg:titleControl.palette.windowText,warning:titleControl.palette.highlight,danger:titleControl.palette.highlight}) }
            }
            Repeater {
                model: root.columns
                delegate: GroupBox {
                    id: columnCard
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; title: root.tr("列") + " " + (index + 1)
                    ColumnLayout {
                        width: parent.width
                        RowLayout {
                            SettingField { label: root.tr("列布局"); kind: "enum"; value: columnCard.modelData.layout; options: [{value:"single",label:root.tr("单块独占")},{value:"split",label:root.tr("上下双块")}]; onEdited: function(v) { root.columns[columnCard.index].layout = v; root.commitColumns(true) } }
                            Button { text: "←"; enabled: columnCard.index > 0; onClicked: root.moveColumn(columnCard.index, -1) }
                            Button { text: "→"; enabled: columnCard.index + 1 < root.columns.length; onClicked: root.moveColumn(columnCard.index, 1) }
                            Button { text: root.tr("删除"); onClicked: { root.columns.splice(columnCard.index,1); root.commitColumns(true) } }
                        }
                        BlockEditor { visible: columnCard.modelData.layout === "single"; Layout.fillWidth: true; blockSpec: columnCard.modelData.single || root.emptyBlock(); accounts: root.accounts; accountRevision:root.previewRevision; onChanged: function(b) { root.columns[columnCard.index].single = b; root.commitColumns(false) } }
                        ColumnLayout {
                            visible: columnCard.modelData.layout === "split"; Layout.fillWidth: true
                            Label { text: root.tr("上方") }
                            BlockEditor { Layout.fillWidth: true; blockSpec: columnCard.modelData.top || root.emptyBlock(); accounts: root.accounts; accountRevision:root.previewRevision; onChanged: function(b) { root.columns[columnCard.index].top = b; root.commitColumns(false) } }
                            Button { text: root.tr("交换上下块"); onClicked: { var c = root.columns[columnCard.index]; var b=c.top; c.top=c.bottom; c.bottom=b; root.commitColumns(true) } }
                            Label { text: root.tr("下方") }
                            BlockEditor { Layout.fillWidth: true; blockSpec: columnCard.modelData.bottom || root.emptyBlock(); accounts: root.accounts; accountRevision:root.previewRevision; onChanged: function(b) { root.columns[columnCard.index].bottom = b; root.commitColumns(false) } }
                        }
                    }
                }
            }
            Button { objectName:"addColumnButton"; text: root.tr("添加列"); onClicked: { root.columns.push({id:"column-"+Date.now()+"-"+root.columns.length,layout:"split",single:root.emptyBlock(),top:root.emptyBlock(),bottom:root.emptyBlock()}); root.commitColumns(true) } }
        }
        ColumnLayout {
            visible: root.section === 1; Layout.fillWidth: true
            Label { text: root.tr("已登录工具的账户自动发现；以下可添加独立账户。"); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: root.accounts
                delegate: GroupBox {
                    id: accountCard
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; title: modelData.name || modelData.providerId
                    ColumnLayout {
                        width: parent.width
                        SettingField { label: root.tr("厂商"); kind:"enum"; value: accountCard.modelData.providerId; options:[{value:"deepseek",label:"DeepSeek"},{value:"glm",label:"GLM"},{value:"opencode-go",label:"OpenCode Go"}]; onEdited: function(v) { root.accounts[accountCard.index].apiUrl=""; root.accounts[accountCard.index].credential=""; root.editAccount(accountCard.index,"providerId",v) } }
                        SettingField { label: root.tr("显示名称"); value: accountCard.modelData.name || ""; onEdited: function(v) { root.editAccount(accountCard.index,"name",v) } }
                        SettingField { label: root.tr("启用"); kind:"bool"; value: accountCard.modelData.enabled !== false; onEdited: function(v) { root.editAccount(accountCard.index,"enabled",v) } }
                        SettingField { label:"API Key / Cookie"; secret:true; value: accountCard.modelData.credential || ""; onEdited: function(v) { root.editAccount(accountCard.index,"credential",v) } }
                        SettingField { visible: accountCard.modelData.providerId === "opencode-go"; label:root.tr("认证方式"); kind:"enum"; value:accountCard.modelData.authMode || "key"; options:[{value:"key",label:"API Key"},{value:"cookie",label:"Cookie"}]; onEdited: function(v) { root.editAccount(accountCard.index,"authMode",v) } }
                        SettingField { label: root.tr("接口地址（留空使用默认）"); value: accountCard.modelData.apiUrl || ""; onEdited: function(v) { root.editAccount(accountCard.index,"apiUrl",v) } }
                        SettingField { visible: accountCard.modelData.providerId === "opencode-go"; label:"Workspace"; value:accountCard.modelData.workspace || ""; onEdited: function(v) { root.editAccount(accountCard.index,"workspace",v) } }
                        SettingField { label:root.tr("查询间隔（秒）"); kind:"int"; minimum:10; maximum:86400; value:accountCard.modelData.intervalSec || 300; onEdited: function(v) { root.editAccount(accountCard.index,"intervalSec",v) } }
                        SettingField { visible: accountCard.modelData.providerId === "deepseek"; label:root.tr("每日起始小时"); kind:"int"; maximum:23; value:accountCard.modelData.startHour || 0; onEdited: function(v) { root.editAccount(accountCard.index,"startHour",v) } }
                        SettingField { visible: accountCard.modelData.providerId === "deepseek"; label:root.tr("每日起始分钟"); kind:"int"; maximum:59; value:accountCard.modelData.startMin || 0; onEdited: function(v) { root.editAccount(accountCard.index,"startMin",v) } }
                        Button { text:root.tr("删除账户"); onClicked: { root.accounts.splice(accountCard.index,1); root.commitAccounts(true) } }
                    }
                }
            }
            Button { objectName:"addAccountButton"; text: root.tr("添加账户"); onClicked: { root.accounts.push({id:"manual-"+Date.now()+"-"+root.accounts.length,providerId:"deepseek",name:"DeepSeek",credential:"",enabled:true,authMode:"key",intervalSec:300,startHour:0,startMin:0}); root.commitAccounts(true) } }
        }
        ColumnLayout {
            visible: root.section === 2; Layout.fillWidth: true
            Repeater {
                model: Data.groups
                delegate: GroupBox {
                    required property var modelData
                    Layout.fillWidth:true; title:root.tr(modelData.group)
                    ColumnLayout {
                        width:parent.width
                        Repeater {
                            model: modelData.fields
                            delegate: SettingField {
                                required property var modelData
                                visible: !modelData.visibleWhen || root.v(modelData.visibleWhen.key, "") === modelData.visibleWhen.equals
                                label:root.tr(modelData.label); kind:modelData.type; value:root.v(modelData.key,modelData.default)
                                minimum:modelData.min || 0; maximum:modelData.max || 86400; secret:!!modelData.secret
                                options:(modelData.options || []).map(function(o) { return {value:o.value,label:root.tr(o.label)} })
                                onEdited: function(v) { if (root.validProxy()) root.plugin.setValue(modelData.key,v) }
                            }
                        }
                    }
                }
            }
        }
    }
}
