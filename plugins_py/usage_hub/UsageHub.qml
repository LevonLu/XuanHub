// UsageHub.qml — py_usage_hub 子插件界面（Bar 部件 + 弹出窗口）
// Bar: 每个厂商紧凑显示，Plan 模式用 QML 进度条（5h/周分两行），余额模式两行文本。
// Popup: 点击打开弹出窗口，每个已启用厂商一个卡片，含进度条/余额显示。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    property var plugin

    width: Math.max(120, barRow.implicitWidth + 16)
    height: 40

    // 色板兜底
    readonly property color cFg:      themeManager ? themeManager.colors.fg : "#cdd6f4"
    readonly property color cMuted:   themeManager ? themeManager.colors.muted : "#7f849c"
    readonly property color cSuccess: themeManager ? themeManager.colors.success : "#a6e3a1"
    readonly property color cDanger:  themeManager ? themeManager.colors.danger : "#f38ba8"
    readonly property color cWarning: themeManager ? themeManager.colors.warning : "#f9e2af"
    readonly property color cPanel:   themeManager ? themeManager.colors.panel : "#313244"
    readonly property color cBorder:  themeManager ? themeManager.colors.border : "#45475a"
    readonly property color cBg:      themeManager ? themeManager.colors.bg : "#1e1e2e"
    readonly property color cAccent:  themeManager ? themeManager.colors.accent : "#89b4fa"

    // 辅助：从 plugin.values 读取
    function vval(vid, key) {
        var pv = plugin ? plugin.values : null
        if (!pv) return undefined
        return pv[vid + "_" + key]
    }

    function barColor(pct) {
        return pct < 50 ? root.cSuccess : pct < 80 ? root.cWarning : root.cDanger
    }

    // ── Bar 内容：左名+右数据的双栏布局 ──────────────────────
    Row {
        id: barRow
        anchors.centerIn: parent
        spacing: 16

        Repeater {
            model: {
                var pv = plugin ? plugin.values : null
                return (pv && pv.vendor_ids) ? pv.vendor_ids : []
            }

            delegate: Item {
                width: vendorWidget.implicitWidth + 4; height: 40

                Row {
                    id: vendorWidget
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // ── 左列：厂商名（自适应宽度）───────
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.vval(modelData, "name") || modelData
                        color: root.cFg; font.pixelSize: 11; font.bold: true
                    }

                    // ── 分隔线 ────────────────────────────
                    Rectangle {
                        width: 1; height: 32; radius: 1
                        color: root.cBorder
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // ── 右列：两行数据 ────────────────────
                    Loader {
                        anchors.verticalCenter: parent.verticalCenter
                        property string vid: modelData
                        sourceComponent: {
                            var mode = root.vval(vid, "mode")
                            return (mode === "plan") ? planBarMini : balanceBarMini
                        }
                    }
                }
            }
        }

        // 兜底：无厂商
        Text {
            text: {
                var pv = plugin ? plugin.values : null
                var ids = (pv && pv.vendor_ids) ? pv.vendor_ids : []
                return ids.length === 0 ? "初始化中..." : ""
            }
            color: root.cMuted; font.pixelSize: 12
            visible: {
                var pv = plugin ? plugin.values : null
                var ids = (pv && pv.vendor_ids) ? pv.vendor_ids : []
                return ids.length === 0
            }
        }
    }

    // ── Mini 余额（两行文本）────────────────────────────
    Component {
        id: balanceBarMini
        Column {
            spacing: 1
            Text {
                text: "今日 " + (root.vval(vid, "today") || "...")
                color: root.cSuccess; font.pixelSize: 10
            }
            Text {
                text: "余额 " + (root.vval(vid, "balance") || "...")
                color: root.cFg; font.pixelSize: 10
            }
        }
    }

    // ── Mini Plan（两行进度条：5h + 周）───────────────
    Component {
        id: planBarMini
        Column {
            spacing: 2
            // 5h 行
            Row {
                spacing: 4
                Text { text: "5h"; color: root.cFg; font.pixelSize: 9; width: 16 }
                Rectangle {
                    width: 50; height: 10; radius: 5; color: root.cBg
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        height: 10; radius: 5
                        readonly property real pct: {
                            var v = root.vval(vid, "plan_5h_pct")
                            return (v !== undefined) ? Math.min(100, Math.max(0, Number(v))) : 0
                        }
                        width: Math.max(0, 50 * pct / 100)
                        color: pct < 50 ? root.cSuccess : pct < 80 ? root.cWarning : root.cDanger
                    }
                }
                Text {
                    text: {
                        var v = root.vval(vid, "plan_5h_pct")
                        return (v !== undefined ? v : 0) + "%"
                    }
                    color: root.cMuted; font.pixelSize: 9; width: 28
                }
            }
            // 每周行
            Row {
                spacing: 4
                Text { text: " W"; color: root.cFg; font.pixelSize: 9; width: 16 }
                Rectangle {
                    width: 50; height: 10; radius: 5; color: root.cBg
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        height: 10; radius: 5
                        readonly property real pct: {
                            var v = root.vval(vid, "plan_weekly_pct")
                            return (v !== undefined) ? Math.min(100, Math.max(0, Number(v))) : 0
                        }
                        width: Math.max(0, 50 * pct / 100)
                        color: pct < 50 ? root.cSuccess : pct < 80 ? root.cWarning : root.cDanger
                    }
                }
                Text {
                    text: {
                        var v = root.vval(vid, "plan_weekly_pct")
                        return (v !== undefined ? v : 0) + "%"
                    }
                    color: root.cMuted; font.pixelSize: 9; width: 28
                }
            }
        }
    }

    // ── Bar 点击 → 切换弹窗 ──────────────────────────────────────
    MouseArea {
        id: clickArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: {
            if (popupWin.visible) { popupWin.hide() }
            else { root.openPopup() }
        }
        onContainsMouseChanged: {
            if (containsMouse) tipDelay.restart()
            else { tipDelay.stop(); tip.hide() }
        }
    }

    // ── 悬停提示气泡（400ms 延迟） ──────────────────────────────
    Timer { id: tipDelay; interval: 400; onTriggered: root.showTip() }

    function showTip() {
        const anchor = root.mapToGlobal(Qt.point(root.width / 2, 0))
        const w = root.Window.window
        let x = Math.round(anchor.x - tip.width / 2)
        let y
        if (w) {
            x = Math.max(w.x + 4, Math.min(x, w.x + w.width - tip.width - 4))
            const atBottom = (typeof barConfig !== "undefined") && barConfig.barEdge === "bottom"
            y = atBottom ? Math.round(w.y - tip.height - 6)
                         : Math.round(w.y + w.height + 6)
        } else {
            y = Math.round(anchor.y + root.height + 6)
        }
        tip.x = x; tip.y = y
        tip.show()
    }

    Window {
        id: tip; visible: false
        width: tipText.implicitWidth + 16; height: tipText.implicitHeight + 10
        color: "transparent"
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
        Rectangle {
            anchors.fill: parent; radius: 5; color: root.cPanel
            border.width: 1; border.color: root.cBorder
            Text {
                id: tipText; anchors.centerIn: parent
                color: root.cFg; font.pixelSize: 11
                text: {
                    const pv = plugin ? plugin.values : null
                    if (!pv) return "点击查看详情"
                    const ids = pv.vendor_ids
                    if (!ids || ids.length === 0) return "点击查看详情"
                    let lines = []
                    for (let i = 0; i < ids.length; i++) {
                        const vid = ids[i]
                        const name = pv[vid + "_name"] || vid
                        const lu = pv[vid + "_last_update"] || "..."
                        lines.push(name + ": " + lu)
                    }
                    return lines.join("\n")
                }
            }
        }
    }

    // ── 弹出窗口 ─────────────────────────────────────────────────
    function openPopup() {
        const pw = 400
        const anchor = root.mapToGlobal(Qt.point(root.width / 2, root.height + 6))
        let x = Math.round(anchor.x - pw / 2)
        let y = Math.round(anchor.y)

        // 只用 bar 窗口做水平越界修正（垂直留给 doClamp 在 show 后处理）
        const bw = root.Window.window
        if (bw) {
            x = Math.max(bw.x + 4, Math.min(x, bw.x + bw.width - pw - 4))
        }

        popupWin.x = x
        popupWin.y = y
        popupWin.show()
        popupWin.raise()
        popupWin.requestActivate()
        popupWin.pendingClamp = true
        rebuildCards()
    }

    function rebuildCards() {
        // 清除旧卡片（保留 emptyLabel）
        const children = cardContainer.children
        for (let i = children.length - 1; i >= 0; i--) {
            if (children[i] !== emptyLabel)
                children[i].destroy()
        }
        const pv = plugin ? plugin.values : null
        if (!pv) return
        const ids = pv.vendor_ids
        if (!ids || ids.length === 0) {
            emptyLabel.visible = true
            return
        }
        emptyLabel.visible = false
        for (let i = 0; i < ids.length; i++) {
            const vid = ids[i]
            const mode = pv[vid + "_mode"] || "balance"
            const comp = (mode === "plan") ? planCardComp : balanceCardComp
            const obj = comp.createObject(cardContainer, {
                "vendorId": vid
            })
            if (obj) obj.Layout.fillWidth = true
        }
    }

    Window {
        id: popupWin
        property bool pendingClamp: false

        visible: false; width: 400
        height: Math.min(520, Math.max(200, cardContainer.implicitHeight + 100))
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        onActiveChanged: if (!active && visible) hide()
        Shortcut { sequence: "Escape"; onActivated: popupWin.hide() }
        onVisibleChanged: if (visible && pendingClamp) Qt.callLater(doClamp)

        Item { id: screenProbe; visible: false }

        function doClamp() {
            pendingClamp = false
            const sg = screenProbe.Screen.virtualGeometry
            if (!sg || sg.width === 0) return
            const w = popupWin.width; const h = popupWin.height
            let x = popupWin.x; let y = popupWin.y
            if (x + w > sg.right)  x = Math.round(sg.right - w)
            if (x < sg.left)       x = sg.left
            if (y + h > sg.bottom) {
                const abovePt = root.mapToGlobal(Qt.point(root.width / 2, -6))
                y = Math.round(abovePt.y - h)
            }
            if (y < sg.top) y = sg.top
            popupWin.x = x; popupWin.y = y
        }

        // ── 内容 ─────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            color: root.cBg; radius: 8

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "用量中心"; color: root.cFg; font.pixelSize: 16; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "✕"; color: root.cMuted; font.pixelSize: 16
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: popupWin.hide()
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                // 卡片列表（滚动）
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded; clip: true

                    ColumnLayout {
                        id: cardContainer; width: parent.width - 8; spacing: 10

                        Text {
                            id: emptyLabel; visible: false
                            text: "无已启用厂商"; color: root.cMuted
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignCenter; Layout.topMargin: 40
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                // 底部刷新按钮
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        implicitWidth: refreshBtnText.implicitWidth + 24
                        implicitHeight: 30; radius: 6
                        color: refreshMa.containsMouse ? root.cBorder : root.cPanel
                        border.width: 1; border.color: root.cBorder
                        Text {
                            id: refreshBtnText; anchors.centerIn: parent
                            text: "⟳ 刷新全部"; color: root.cAccent; font.pixelSize: 12
                        }
                        MouseArea {
                            id: refreshMa; anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (plugin) plugin.call("refresh", [])
                                Qt.callLater(rebuildCards)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 卡片组件定义 ─────────────────────────────────────────────

    // 辅助：从 plugin.values 读取厂商数据
    function vendorVal(vendorId, key) {
        const pv = plugin ? plugin.values : null
        if (!pv) return undefined
        return pv[vendorId + "_" + key]
    }

    // ── Balance 卡片 ─────────────────────────────────────────────
    Component {
        id: balanceCardComp
        Rectangle {
            id: card
            property string vendorId: ""

            Layout.fillWidth: true
            implicitHeight: cardCol.implicitHeight + 20
            radius: 8; color: root.cPanel
            border.width: 1; border.color: root.cBorder

            ColumnLayout {
                id: cardCol; anchors.fill: parent; anchors.margins: 10; spacing: 6

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: 8; implicitHeight: 8; radius: 4
                        color: root.vendorVal(card.vendorId, "status") === "ok" ? root.cSuccess
                             : root.vendorVal(card.vendorId, "status") === "error" ? root.cDanger
                             : root.cWarning
                    }
                    Text {
                        text: root.vendorVal(card.vendorId, "name") || card.vendorId
                        color: root.cFg; font.pixelSize: 13; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "余额"; color: root.cMuted; font.pixelSize: 10 }
                }

                // 数据行
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: {
                        const st = root.vendorVal(card.vendorId, "status")
                        return st === "ok" ? balanceDataOk : balanceDataErr
                    }
                }

                Component {
                    id: balanceDataOk
                    ColumnLayout {
                        spacing: 2
                        Text {
                            text: "今日用量: " + (root.vendorVal(card.vendorId, "today") || "...")
                            color: root.cSuccess; font.pixelSize: 12
                        }
                        Text {
                            text: "当前余额: " + (root.vendorVal(card.vendorId, "balance") || "...")
                            color: root.cFg; font.pixelSize: 12
                        }
                    }
                }

                Component {
                    id: balanceDataErr
                    Text {
                        text: {
                            const st = root.vendorVal(card.vendorId, "status")
                            if (st === "error") return "错误: " + (root.vendorVal(card.vendorId, "error") || "未知")
                            return "查询中..."
                        }
                        color: root.vendorVal(card.vendorId, "status") === "error" ? root.cDanger : root.cMuted
                        font.pixelSize: 11
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "更新: " + (root.vendorVal(card.vendorId, "last_update") || "...")
                        color: root.cMuted; font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "⟳"; color: root.cAccent; font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (plugin) plugin.call("refresh_vendor", [card.vendorId])
                                Qt.callLater(rebuildCards)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Plan 卡片 ────────────────────────────────────────────────
    Component {
        id: planCardComp
        Rectangle {
            id: planCard
            property string vendorId: ""

            Layout.fillWidth: true
            implicitHeight: planCol.implicitHeight + 20
            radius: 8; color: root.cPanel
            border.width: 1; border.color: root.cBorder

            ColumnLayout {
                id: planCol; anchors.fill: parent; anchors.margins: 10; spacing: 6

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: 8; implicitHeight: 8; radius: 4
                        color: root.vendorVal(planCard.vendorId, "status") === "ok" ? root.cSuccess
                             : root.vendorVal(planCard.vendorId, "status") === "error" ? root.cDanger
                             : root.cWarning
                    }
                    Text {
                        text: root.vendorVal(planCard.vendorId, "name") || planCard.vendorId
                        color: root.cFg; font.pixelSize: 13; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "Plan"; color: root.cMuted; font.pixelSize: 10 }
                }

                // 数据区
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: {
                        const st = root.vendorVal(planCard.vendorId, "status")
                        return st === "ok" ? planDataOk : planDataErr
                    }
                }

                Component {
                    id: planDataErr
                    Text {
                        text: {
                            const st = root.vendorVal(planCard.vendorId, "status")
                            if (st === "error") return "错误: " + (root.vendorVal(planCard.vendorId, "error") || "未知")
                            return "查询中..."
                        }
                        color: root.vendorVal(planCard.vendorId, "status") === "error" ? root.cDanger : root.cMuted
                        font.pixelSize: 11
                    }
                }

                Component {
                    id: planDataOk
                    ColumnLayout {
                        spacing: 8

                        // 5h 窗口
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: plan5hCol.implicitHeight + 12
                            radius: 6; color: root.cBg
                            border.width: 1; border.color: root.cBorder

                            ColumnLayout {
                                id: plan5hCol
                                anchors.fill: parent; anchors.margins: 8; spacing: 4

                                // 标题 + 用量 + 刷新时间（同一行）
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "5小时窗口"
                                        color: root.cFg; font.pixelSize: 11; font.bold: true
                                    }
                                    Text {
                                        text: root.vendorVal(planCard.vendorId, "plan_5h_detail") || "-"
                                        color: root.cMuted; font.pixelSize: 10
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: "刷新: " + (root.vendorVal(planCard.vendorId, "plan_5h_reset") || "-")
                                        color: root.cMuted; font.pixelSize: 10
                                    }
                                }

                                // 进度条 + 百分比并排
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6

                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 14
                                        radius: 7; color: root.cPanel

                                        Rectangle {
                                            id: bar5h
                                            anchors.left: parent.left
                                            anchors.top: parent.top; anchors.bottom: parent.bottom
                                            radius: 7
                                            readonly property real pct: {
                                                var v = root.vendorVal(planCard.vendorId, "plan_5h_pct")
                                                return (v !== undefined && v !== null) ? Math.min(100, Math.max(0, Number(v))) : 0
                                            }
                                            width: Math.max(bar5h.radius * 2, parent.width * bar5h.pct / 100)
                                            color: bar5h.pct < 50 ? root.cSuccess : bar5h.pct < 80 ? root.cWarning : root.cDanger
                                        }
                                    }

                                    Text {
                                        text: (root.vendorVal(planCard.vendorId, "plan_5h_pct") || 0) + "%"
                                        color: root.cFg; font.pixelSize: 12; font.bold: true
                                        Layout.preferredWidth: 35
                                    }
                                }
                            }
                        }

                        // 每周窗口
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: planWCol.implicitHeight + 12
                            radius: 6; color: root.cBg
                            border.width: 1; border.color: root.cBorder

                            ColumnLayout {
                                id: planWCol
                                anchors.fill: parent; anchors.margins: 8; spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "每周窗口"
                                        color: root.cFg; font.pixelSize: 11; font.bold: true
                                    }
                                    Text {
                                        text: root.vendorVal(planCard.vendorId, "plan_weekly_detail") || "-"
                                        color: root.cMuted; font.pixelSize: 10
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: "刷新: " + (root.vendorVal(planCard.vendorId, "plan_weekly_reset") || "-")
                                        color: root.cMuted; font.pixelSize: 10
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true; spacing: 6

                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 14
                                        radius: 7; color: root.cPanel

                                        Rectangle {
                                            id: barW
                                            anchors.left: parent.left
                                            anchors.top: parent.top; anchors.bottom: parent.bottom
                                            radius: 7
                                            readonly property real pct: {
                                                var v = root.vendorVal(planCard.vendorId, "plan_weekly_pct")
                                                return (v !== undefined && v !== null) ? Math.min(100, Math.max(0, Number(v))) : 0
                                            }
                                            width: Math.max(barW.radius * 2, parent.width * barW.pct / 100)
                                            color: barW.pct < 50 ? root.cSuccess : barW.pct < 80 ? root.cWarning : root.cDanger
                                        }
                                    }

                                    Text {
                                        text: (root.vendorVal(planCard.vendorId, "plan_weekly_pct") || 0) + "%"
                                        color: root.cFg; font.pixelSize: 12; font.bold: true
                                        Layout.preferredWidth: 35
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "更新: " + (root.vendorVal(planCard.vendorId, "last_update") || "...")
                        color: root.cMuted; font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "⟳"; color: root.cAccent; font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (plugin) plugin.call("refresh_vendor", [planCard.vendorId])
                                Qt.callLater(rebuildCards)
                            }
                        }
                    }
                }
            }
        }
    }
}
