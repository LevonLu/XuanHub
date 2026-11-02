// AgentDetail.qml — 单工具日期区间查询、Token 分项和按日/模型明细
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "TokenFormat.js" as TokenFormat

Item {
    id: detail
    objectName: "agentDetail"
    property var plugin
    property var pluginContext
    property var values: ({})
    property var colors
    property var agent
    property var translate
    property var sharedStatusText
    signal backRequested()
    property string requestId: ""
    property string callId: ""
    property int serial: 0
    property string submittedFrom: ""
    property string submittedTo: ""
    property string localError: ""
    property string viewMode: "day"
    readonly property var response: values.detail || ({})
    readonly property bool edited: fromDate.text !== submittedFrom || toDate.text !== submittedTo
    readonly property bool matches: response.requestId === requestId && agent && response.sourceId === agent.id
    readonly property string status: localError || (edited ? "editing" : matches ? response.status : "loading")
    readonly property var detailData: status === "ok" && response.data ? response.data : null
    readonly property var rows: detailData ? (viewMode === "day" ? detailData.byDay : detailData.byModel) : []
    readonly property real maximum: rows.reduce(function(max, row) { return Math.max(max, row.total_tokens) }, 1)

    function tr(s) { return translate ? translate(s) : qsTranslate("plugin.py_agent_usage.main", s) }
    function number(n) { return TokenFormat.compact(n) }
    function dateText(date) { return Qt.formatDate(date, "yyyy-MM-dd") }
    function validDate(s) {
        if (!/^\d{4}-\d{2}-\d{2}$/.test(s)) return false
        var parts = s.split("-")
        var date = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]))
        return Number(parts[0]) >= 1970 && dateText(date) === s
    }
    function submit() {
        localError = ""
        if (!validDate(fromDate.text) || !validDate(toDate.text)) { localError = "invalid_date"; return }
        if (fromDate.text > toDate.text) { localError = "invalid_range"; return }
        submittedFrom = fromDate.text
        submittedTo = toDate.text
        requestId = String(Date.now()) + "-" + (++serial)
        if (!plugin || !agent) { localError = "collector_failed"; return }
        var ticket = plugin.call("query_detail", [agent.id, submittedFrom, submittedTo, requestId])
        if (!ticket || !ticket.queued) { localError = "collector_failed"; return }
        callId = String(ticket.id || "")
        if (status === "loading") queryDeadline.restart()
    }
    function quickRange(days) {
        var end = new Date()
        var start = new Date(end.getFullYear(), end.getMonth(), end.getDate())
        start.setDate(start.getDate() - days + 1)
        fromDate.text = dateText(start)
        toDate.text = dateText(end)
        submit()
    }
    function message() {
        switch (status) {
        case "editing": return tr("日期已修改，点击查询更新明细")
        case "invalid_date": return tr("请输入有效日期，格式为 YYYY-MM-DD")
        case "invalid_range": return tr("开始日期不能晚于结束日期")
        case "loading": return tr("正在读取历史用量…")
        case "data_not_ready": return tr("尚未完成首次采集，请稍后重新查询")
        case "cancelled": return tr("配置已变化，请重新查询")
        case "timeout": return tr("查询超时，请重试")
        case "ok": return detailData && detailData.sourceError ? tr("此来源暂时无法读取，显示已有记录") : detailData && detailData.totals.estimated ? tr("部分 Token 根据工具日志估算") : detailData && !detailData.hasRecords ? tr("该时间段没有已采集的记录") : ""
        default: return sharedStatusText ? sharedStatusText(status) : tr("查询失败，请重试")
        }
    }
    Component.onCompleted: {
        var range = values.defaultRange || ({})
        fromDate.text = range.from || dateText(new Date())
        toDate.text = range.to || dateText(new Date())
        submit()
    }
    Connections {
        target: detail.plugin || null
        ignoreUnknownSignals: true
        function onCallFinished(id, ok, result, error) {
            if (String(id) === detail.callId && !ok) detail.localError = "collector_failed"
        }
    }
    onStatusChanged: if (status !== "loading") queryDeadline.stop()
    Timer {
        id: queryDeadline; interval: 45000
        onTriggered: detail.localError = "timeout"
    }

    component Action: Rectangle {
        property string label
        signal triggered()
        implicitWidth: text.implicitWidth + 22; implicitHeight: 30; radius: 6; color: detail.colors.panel
        Text { id: text; anchors.centerIn: parent; text: parent.label; font.pixelSize: 12; color: detail.colors.accent }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.triggered() }
    }
    component DateInput: TextField {
        implicitHeight: 32; Layout.fillWidth: true; Layout.minimumWidth: 100
        placeholderText: "YYYY-MM-DD"; font.pixelSize: 12
        color: detail.colors.fg; placeholderTextColor: detail.colors.muted; selectionColor: detail.colors.accent
        background: Rectangle { color: detail.colors.panel; radius: 6; border.color: parent.activeFocus ? detail.colors.accent : detail.colors.border }
        onTextEdited: detail.localError = ""
        onAccepted: detail.submit()
        selectByMouse: true; maximumLength: 10
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Action { objectName: "detailBack"; label: "‹ " + detail.tr("工具列表"); onTriggered: detail.backRequested() }
            Text { Layout.fillWidth: true; text: detail.agent ? detail.agent.name : ""; textFormat: Text.PlainText; elide: Text.ElideRight; color: detail.colors.fg; font.pixelSize: 17; font.bold: true }
        }
        Text { text: detail.tr("选择时间段（本地日期，含起止两天）"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.muted; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: detail.tr("开始日期"); color: detail.colors.muted; font.pixelSize: 11 }
                DateInput { id: fromDate; objectName: "detailFromDate" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: detail.tr("结束日期"); color: detail.colors.muted; font.pixelSize: 11 }
                DateInput { id: toDate; objectName: "detailToDate" }
            }
            Action { objectName: "detailQuery"; label: detail.tr("查询"); Layout.alignment: Qt.AlignBottom; implicitHeight: 32; onTriggered: detail.submit() }
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: [1, 7, 30]
                delegate: Action {
                    required property int modelData
                    objectName: "detailQuick" + modelData
                    label: modelData === 1 ? detail.tr("今天") : modelData === 7 ? detail.tr("近 7 天") : detail.tr("近 30 天")
                    onTriggered: detail.quickRange(modelData)
                }
            }
        }
        Text { objectName: "detailMessage"; text: detail.message(); visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.warning; font.pixelSize: 12 }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: summary.implicitHeight + 24; radius: 8; color: detail.colors.panel
            ColumnLayout {
                id: summary; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                Text { objectName: "detailTotal"; text: detail.detailData ? detail.number(detail.detailData.totals.total_tokens) : "—"; color: detail.colors.fg; font.pixelSize: 28; font.bold: true; Layout.fillWidth: true }
                Text { text: detail.tr("区间 Token 总量"); color: detail.colors.muted; font.pixelSize: 11; Layout.fillWidth: true }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.muted; font.pixelSize: 11; visible: !!detail.detailData
                    text: detail.detailData ? detail.tr("输入 %1 · 输出 %2 · 缓存读取 %3 · 缓存写入 %4").arg(detail.number(detail.detailData.totals.input_tokens)).arg(detail.number(detail.detailData.totals.output_tokens)).arg(detail.number(detail.detailData.totals.cached_input_tokens)).arg(detail.number(detail.detailData.totals.cache_creation_input_tokens)) : ""
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.muted; font.pixelSize: 11; visible: !!detail.detailData
                    text: detail.detailData ? detail.tr("推理 %1（已包含在输出中）").arg(detail.number(detail.detailData.totals.reasoning_output_tokens)) : ""
                }
            }
        }
        RowLayout {
            spacing: 8
            Action { objectName: "detailDaily"; label: detail.tr("按日明细"); border.color: detail.viewMode === "day" ? detail.colors.accent : "transparent"; onTriggered: detail.viewMode = "day" }
            Action { objectName: "detailModels"; label: detail.tr("按模型明细"); border.color: detail.viewMode === "model" ? detail.colors.accent : "transparent"; onTriggered: detail.viewMode = "model" }
        }
        ListView {
            id: breakdown; objectName: "detailBreakdown"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 7
            model: detail.rows
            ScrollBar.vertical: ScrollBar {
                width: 6; policy: ScrollBar.AsNeeded
                contentItem: Rectangle { implicitWidth: 6; radius: 3; color: detail.colors.muted; opacity: 0.6 }
                background: Item {}
            }
            delegate: Rectangle {
                required property var modelData
                width: breakdown.width - 8; height: 50; radius: 6; color: detail.colors.panel
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: detail.viewMode === "day" ? modelData.day : modelData.model; color: detail.colors.fg; font.pixelSize: 12 }
                        Text { text: detail.number(modelData.total_tokens) + " tokens"; color: detail.colors.fg; font.pixelSize: 12 }
                    }
                    Rectangle {
                        Layout.fillWidth: true; height: 3; radius: 2; color: detail.colors.border
                        Rectangle { height: 3; width: parent.width * modelData.total_tokens / detail.maximum; radius: 2; color: detail.colors.accent }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.muted; font.pixelSize: 10
            text: detail.agent && detail.agent.scope === "account" ? detail.tr("账号级数据，可能包含其他设备") : detail.tr("本机记录")
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: detail.colors.muted; font.pixelSize: 10
            text: detail.tr("基于已采集历史；最近采集：%1").arg(detail.response.usageUpdated || detail.values.usageUpdated || "—")
        }
    }
}
