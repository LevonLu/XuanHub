// tst_TokenUsage.qml — 新列编辑器、草稿隔离和账户页真实组件回归
import QtQuick
import QtQuick.Controls
import QtTest
import "../../agent_usage" as Usage

TestCase {
    id: tests
    name: "TokenUsage"
    when: windowShown
    width: 900; height: 800; visible: true
    property var settings
    property var draft: ({})
    QtObject {
        id: proxy
        property string pluginId: "py_agent_usage"
        property int revision: 0
        function value(key) { return tests.draft[key] }
        function setValue(key,value) { var next=JSON.parse(JSON.stringify(tests.draft)); next[key]=value; tests.draft=next; revision++ }
    }
    Component { id: settingsComponent; Usage.TokenSettings { width: 800 } }
    Component { id: barComponent; Usage.BarColumns {} }
    Component { id: panelComponent; Usage.TokenPanel { width:420; height:570; colors:({bg:"#1e1e2e",panel:"#313244",fg:"#cdd6f4",muted:"#9399b2",border:"#45475a",accent:"#89b4fa",warning:"#f9e2af",danger:"#f38ba8"}) } }
    function init() {
        proxy.pluginId="py_agent_usage"
        draft={barColumns:[{id:"a",layout:"single",single:{metric:"tokens",targetId:"total",label:""},top:{metric:"quota5h",targetId:"auto:codex"},bottom:{metric:"quota7d",targetId:"auto:codex"}}],accounts:[]}
        settings=createTemporaryObject(settingsComponent,tests,{plugin:proxy})
        verify(settings !== null)
    }
    function test_layout_switch_swap_and_move_preserve_blocks() {
        var before=JSON.stringify(settings.columns[0].single)
        settings.columns[0].layout="split"; settings.commitColumns(true)
        compare(JSON.stringify(draft.barColumns[0].single),before)
        var c=settings.columns[0]; var t=c.top; c.top=c.bottom; c.bottom=t; settings.commitColumns(true)
        compare(draft.barColumns[0].top.metric,"quota7d")
        settings.columns.push({id:"b",layout:"single",single:{metric:"tokens",targetId:"codex"},top:{metric:"empty"},bottom:{metric:"empty"}})
        settings.commitColumns(true); settings.moveColumn(1,-1)
        compare(draft.barColumns[0].id,"b")
        settings.columns.splice(0,1); settings.commitColumns(true)
        compare(draft.barColumns.length,1)
    }
    function test_add_buttons_write_draft() {
        var addColumn=findChild(settings,"addColumnButton")
        verify(addColumn !== null)
        mouseClick(addColumn)
        compare(draft.barColumns.length,2)
        settings.section=1
        wait(20)
        var addAccount=findChild(settings,"addAccountButton")
        verify(addAccount !== null)
        mouseClick(addAccount)
        compare(draft.accounts.length,1)
        compare(draft.accounts[0].providerId,"deepseek")
    }
    function test_switching_plugin_does_not_write_other_draft() {
        var before=JSON.stringify(draft)
        proxy.pluginId="py_other"
        settings.columns=[]; settings.commitColumns(true); settings.accounts=[]; settings.commitAccounts(true)
        compare(JSON.stringify(draft),before)
    }
    function test_preview_and_single_split_width() {
        var columns=JSON.parse(JSON.stringify(settings.preview))
        var bar=createTemporaryObject(barComponent,tests,{columns:columns})
        verify(bar !== null); verify(bar.width>10)
        var singleWidth=bar.width
        columns.push({id:"b",layout:"split",single:{metric:"empty"},top:{metric:"quota5h",name:"Codex",value:0},bottom:{metric:"balance",name:"DeepSeek",value:"10.00",currency:"CNY"}})
        bar.columns=columns
        tryVerify(function(){return bar.width>singleWidth})
        var labels=[]
        function collect(item) { if(item.text !== undefined) labels.push(item.text); for(var child of item.children || []) collect(child) }
        collect(bar)
        verify(labels.indexOf("Codex 5h 0%")>=0)
        verify(labels.indexOf("DeepSeek 余额 ¥10.00")>=0)
    }
    function test_account_page_independent_of_tool_filter() {
        var panel=createTemporaryObject(panelComponent,tests,{values:{agents:[],popupAgentMode:"active",accounts:[{id:"a",name:"DeepSeek",status:"ok",balances:[{currency:"CNY",balance:"12.30",spendToday:"0.20",since:"2026-11-02T09:00:00",affected:true}],windows:[]}]}})
        verify(panel !== null)
        panel.page=1
        verify(waitForRendering(panel))
        compare(panel.values.accounts.length,1)
    }
    function test_only_selected_popup_page_exists() {
        var panel=createTemporaryObject(panelComponent,tests,{values:{agents:[],accounts:[]}})
        verify(panel !== null)
        verify(findChild(panel,"agentUsagePanel") !== null)
        compare(findChild(panel,"tokenAccountPanel"),null)
        panel.page=1
        tryCompare(panel,"page",1)
        verify(findChild(panel,"tokenAccountPanel") !== null)
        tryVerify(function(){return findChild(panel,"agentUsagePanel") === null})
        panel.page=0
        verify(findChild(panel,"agentUsagePanel") !== null)
        tryVerify(function(){return findChild(panel,"tokenAccountPanel") === null})
    }
}
