// tst_AgentUsageDetails.qml — 真实 QML 点击、日期校验、过期回执与详情导航回归
import QtQuick
import QtTest
import "../../agent_usage" as Usage

TestCase {
    id: tests
    name: "AgentUsageDetails"
    when: windowShown
    visible: true
    width: 640; height: 700
    property var panel
    QtObject {
        id: fake
        property var values: ({})
        property var calls: []
        signal callFinished(string id, bool ok, var result, var error)
        function call(name, args) {
            var id = "call-" + (calls.length + 1)
            calls = calls.concat([{ name: name, args: args, id: id }])
            return { queued: true, id: id }
        }
    }
    Component {
        id: panelComponent
        Usage.UsagePanel { plugin: fake; values: fake.values }
    }
    SignalSpy { id: refreshSpy; target: tests.panel || null; signalName: "refreshRequested" }
    function init() {
        fake.calls = []
        fake.values = { defaultRange: {from:"2026-10-23",to:"2026-10-29"}, usageStatus:"ok", quotaStatus:"ok",
            agents:[{id:"codex",name:"Codex",scope:"local",stats:null,quota:{status:"pending",windows:[]}}] }
        panel = createTemporaryObject(panelComponent, tests)
        verify(panel !== null)
        verify(waitForRendering(panel))
    }
    function openDetail() {
        var card = findChild(panel, "agentCard_codex")
        verify(card !== null)
        mouseClick(card, 30, 15)
        tryVerify(function() { return panel.selectedAgent !== null })
        tryVerify(function() { return fake.calls.length === 1 })
        var detail = findChild(panel, "agentDetail")
        verify(detail !== null)
        return detail
    }
    function respond(call, count) {
        var args = call.args
        fake.values = Object.assign({}, fake.values, {detail:{requestId:args[3],sourceId:args[0],fromDate:args[1],toDate:args[2],
            status:"ok",data:{hasRecords:count > 0,totals:{total_tokens:count,cost:0.25},
                byDay:[{day:args[1],total_tokens:count,cost:0.25}],byModel:[{model:"test-model",total_tokens:count,cost:0.25}]}}})
    }
    function verifyNoMoney(item) {
        if (typeof item.text === "string")
            verify(!/\$|USD|费用|缺少价格/.test(item.text), "意外显示金额：" + item.text)
        for (var i = 0; i < item.children.length; ++i)
            verifyNoMoney(item.children[i])
    }
    function test_legacy_costs_are_not_displayed() {
        fake.values = Object.assign({}, fake.values, {usageUpdated:"2026-11-02 10:00",totalTokens:123,
            totalCost:99,unpriced:true,agents:[{id:"codex",name:"Codex",scope:"local",
                stats:{total_tokens:123,cost:99,unpriced:true},quota:{status:"pending",windows:[]}}]})
        verify(waitForRendering(panel))
        verifyNoMoney(panel)
        var detail = openDetail()
        respond(fake.calls[0], 123)
        tryCompare(detail,"status","ok")
        compare(findChild(detail,"detailTotal").text,"123")
        verify(waitForRendering(detail))
        verifyNoMoney(panel)
        mouseClick(findChild(detail,"detailModels"),10,10)
        compare(detail.rows[0].model,"test-model")
        verify(waitForRendering(detail))
        verifyNoMoney(panel)
    }
    function test_click_query_and_back() {
        var detail = openDetail()
        compare(fake.calls[0].name, "query_detail")
        compare(fake.calls[0].args.slice(0,3), ["codex","2026-10-23","2026-10-29"])
        respond(fake.calls[0], 123)
        tryCompare(detail,"status","ok")
        compare(findChild(detail,"detailTotal").text,"123")
        mouseClick(findChild(detail,"detailModels"),10,10)
        compare(detail.viewMode,"model")
        compare(detail.rows[0].model,"test-model")
        mouseClick(findChild(detail,"detailBack"),10,10)
        tryCompare(panel,"selectedAgent",null)
        verify(findChild(panel,"agentCard_codex").visible)
    }
    function test_custom_dates_and_stale_response() {
        var detail = openDetail()
        var old = fake.calls[0]
        respond(old, 100)
        findChild(detail,"detailFromDate").text = "2026-10-01"
        compare(detail.status,"editing")
        compare(findChild(detail,"detailTotal").text,"—")
        mouseClick(findChild(detail,"detailQuery"),10,10)
        compare(fake.calls[1].args.slice(0,3), ["codex","2026-10-01","2026-10-29"])
        respond(old, 900)
        compare(detail.status,"loading")
        respond(fake.calls[1], 250)
        tryCompare(detail,"status","ok")
        compare(findChild(detail,"detailTotal").text,"250")
    }
    function test_invalid_dates_do_not_query() {
        var detail = openDetail()
        findChild(detail,"detailFromDate").text = "2026-02-30"
        mouseClick(findChild(detail,"detailQuery"),10,10)
        compare(detail.status,"invalid_date")
        compare(fake.calls.length,1)
        findChild(detail,"detailFromDate").text = "2026-10-30"
        mouseClick(findChild(detail,"detailQuery"),10,10)
        compare(detail.status,"invalid_range")
        compare(fake.calls.length,1)
    }
    function test_quick_range_and_worker_error() {
        var detail = openDetail()
        mouseClick(findChild(detail,"detailQuick7"),10,10)
        compare(fake.calls.length,2)
        var now = new Date()
        var start = new Date(now.getFullYear(),now.getMonth(),now.getDate())
        start.setDate(start.getDate()-6)
        compare(fake.calls[1].args[1],Qt.formatDate(start,"yyyy-MM-dd"))
        fake.callFinished(fake.calls[1].id,false,null,{code:"worker_stopped"})
        compare(detail.status,"collector_failed")
    }
    function test_selection_update_closes_hidden_detail() {
        openDetail()
        fake.values = Object.assign({}, fake.values, {agents:[], popupFiltered:true, hiddenAgentIds:["codex"]})
        tryCompare(panel,"selectedAgent",null)
        tryVerify(function() { return findChild(panel,"agentDetail") === null })
        var empty = findChild(panel,"agentEmptyState")
        verify(empty.visible)
        verify(empty.text.indexOf("插件设置") >= 0)
        verify(findChild(panel,"agentFilterHint") === null)
        compare(fake.calls.length,1)
    }
    function test_selection_update_retains_visible_detail() {
        var detail = openDetail()
        respond(fake.calls[0], 123)
        fake.values = Object.assign({}, fake.values, {popupFiltered:true, hiddenAgentIds:["claude"]})
        compare(panel.selectedAgent.id,"codex")
        compare(detail.status,"ok")
        compare(findChild(detail,"detailTotal").text,"123")
        compare(fake.calls.length,1)
    }
    function test_active_period_empty_state() {
        openDetail()
        fake.values = Object.assign({}, fake.values, {agents:[], popupFiltered:true,
            popupAgentMode:"active", hiddenAgentIds:["codex"]})
        tryCompare(panel,"selectedAgent",null)
        var empty = findChild(panel,"agentEmptyState")
        verify(empty.visible)
        compare(empty.text,"本统计周期尚无 Token 消耗记录。")
        verify(findChild(panel,"agentFilterHint") === null)
    }
    function test_compact_popup_tracks_content_and_scrolls() {
        compare(panel.width,420)
        verify(panel.height < 400)
        verify(findChild(panel,"agentOverviewTotal") !== null)
        var initialHeight = panel.height
        var many = []
        for (var i = 0; i < 18; ++i)
            many.push({id:"tool-"+i,name:"Agent "+i,scope:"local",stats:{total_tokens:100},quota:{status:"pending",windows:[]}})
        fake.values = Object.assign({}, fake.values, {agents:many})
        tryVerify(function() { return panel.height > initialHeight })
        compare(panel.height,620)
        var list = findChild(panel,"agentListViewport")
        verify(list.contentHeight > list.height)
        list.contentY = list.contentHeight - list.height
        verify(list.contentY > 0)
        fake.values = Object.assign({}, fake.values, {agents:[]})
        tryVerify(function() { return panel.height < 400 })
    }
    function test_refresh_remains_available() {
        refreshSpy.clear()
        var button = findChild(panel,"agentRefreshButton")
        verify(button !== null)
        mouseClick(button,10,10)
        compare(refreshSpy.count,1)
    }
}
