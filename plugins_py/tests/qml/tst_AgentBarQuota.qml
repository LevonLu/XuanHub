// tst_AgentBarQuota.qml — bar 额度的周期、未知值、缓存标记和自然宽度回归
import QtQuick
import QtTest
import "../../agent_usage" as Usage

TestCase {
    id: tests
    name: "AgentBarQuota"
    when: windowShown
    visible: true
    width: 1000; height: 80
    property var summary
    Component { id: component; Usage.BarQuotaSummary {} }
    function init() {
        summary = createTemporaryObject(component, tests)
        verify(summary !== null)
    }
    function test_usage_with_single_period_data() {
        return [{tag:"5h",period:"5h"},{tag:"7d",period:"7d"}]
    }
    function test_usage_with_single_period(data) {
        summary.usageText = "3.67M"
        summary.entries = [{id:"codex",name:"Codex",windows:[{period:data.period,used:42}]}]
        verify(waitForRendering(summary))
        verify(summary.stackUsage)
        var total = findChild(summary,"agentUsageLabel")
        var rows = findChild(summary,"agentBarQuotaRows")
        compare(total.text,"3.67M")
        compare(rows.x,total.x)
        verify(rows.y >= total.y + total.height)
        verify(summary.width >= Math.max(total.width,rows.width))
        verify(rows.y + rows.height <= summary.height)
    }
    function test_usage_with_both_periods_stays_in_separate_column() {
        summary.usageText = "用量 3.67M"
        summary.entries = [{id:"codex",name:"Codex",windows:[{period:"5h",used:42},{period:"7d",used:68}]}]
        verify(waitForRendering(summary))
        verify(!summary.stackUsage)
        var total = findChild(summary,"agentUsageLabel")
        var rows = findChild(summary,"agentBarQuotaRows")
        var shortWindow = findChild(summary,"barQuota_codex_5h")
        var weekly = findChild(summary,"barQuota_codex_7d")
        verify(rows.x >= total.x + total.width)
        compare(shortWindow.x,weekly.x)
        verify(weekly.y >= shortWindow.y + shortWindow.height)
        verify(total.y > rows.y)
        verify(total.y + total.height <= summary.height)
    }
    function test_mixed_sources_align_period_rows() {
        summary.usageText = "3.67M"
        summary.entries = [
            {id:"claude",name:"Claude Code",windows:[{period:"5h",used:10}]},
            {id:"codex",name:"Codex",windows:[{period:"7d",used:20}]}]
        verify(waitForRendering(summary))
        verify(summary.hasBothPeriods)
        verify(!summary.stackUsage)
        var shortWindow = findChild(summary,"barQuota_claude_5h")
        var weekly = findChild(summary,"barQuota_codex_7d")
        verify(weekly.mapToItem(summary,0,0).y >= shortWindow.mapToItem(summary,0,0).y + shortWindow.height)
    }
    function test_usage_only_and_long_prefix() {
        summary.usageText = "3.67M"
        verify(waitForRendering(summary))
        var total = findChild(summary,"agentUsageLabel")
        verify(!summary.stackUsage)
        compare(total.y,0)
        compare(summary.implicitWidth,total.width)
        summary.usageText = "<b>这是较长的用户用量前缀</b> ".repeat(8) + "3.67M"
        verify(waitForRendering(summary))
        verify(total.width <= 212)
        compare(total.textFormat,Text.PlainText)
    }
    function test_empty_and_live_settings_update() {
        compare(summary.implicitWidth, 0)
        summary.entries = [{id:"codex",name:"Codex",windows:[{period:"5h",used:0},{period:"7d",used:73.5}]}]
        var text = findChild(summary,"barQuota_codex_5h")
        verify(text !== null)
        compare(text.text,"Codex 5h 0%")
        var weekly = findChild(summary,"barQuota_codex_7d")
        compare(weekly.text,"Codex 7d 73.5%")
        verify(weekly.y >= text.y + text.height)
        tryVerify(function() { return summary.implicitWidth > 0 })
        summary.entries = [{id:"codex",name:"Codex",windows:[{period:"7d",used:73.5}]}]
        compare(findChild(summary,"barQuota_codex_7d").text,"Codex 7d 73.5%")
        summary.entries = []
        tryCompare(summary,"implicitWidth",0)
        verify(findChild(summary,"barQuota_codex") === null)
    }
    function test_unknown_cached_and_warning() {
        summary.entries = [{id:"codex",name:"Codex",stale:true,windows:[{period:"5h",used:25},{period:"7d",used:null}]}]
        var text = findChild(summary,"barQuota_codex_5h")
        compare(text.text,"Codex 5h 25% *")
        compare(findChild(summary,"barQuota_codex_7d").text,"Codex 7d — *")
        compare(text.color, summary.colors.warning)
        summary.entries = [{id:"codex",name:"Codex",windows:[{period:"5h",used:90}]}]
        compare(findChild(summary,"barQuota_codex_5h").color,summary.colors.danger)
    }
    function test_multiple_providers_expand_width_and_plain_text() {
        summary.entries = [
            {id:"agent-plan",name:"Ark Agent Plan",windows:[{period:"5h",used:10},{period:"7d",used:20}]},
            {id:"codex",name:"Codex",windows:[{period:"5h",used:30},{period:"7d",used:40}]},
            {id:"codex-spark",name:"Codex Spark",windows:[{period:"5h",used:50}]}]
        verify(waitForRendering(summary))
        var first = findChild(summary,"barQuota_agent-plan")
        var second = findChild(summary,"barQuota_codex")
        var third = findChild(summary,"barQuota_codex-spark")
        verify(second.x >= first.x + first.width)
        verify(third.x >= second.x + second.width)
        verify(summary.implicitWidth >= third.x + third.width)
        compare(findChild(summary,"barQuota_codex-spark_5h").textFormat,Text.PlainText)
    }
}
