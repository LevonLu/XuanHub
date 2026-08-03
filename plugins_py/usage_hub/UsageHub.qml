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

    // childrenRect 取实际几何（不走 implicit 链——positioner/Rectangle 的 implicit 语义不可靠）
    width: Math.max(120, barRow.childrenRect.width + 16)
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

    // bar hover 反馈（提示部件可点击弹窗）；clickArea 在下方声明，运行时解析无碍
    property color cHover: themeManager ? themeManager.colors.hover : "#45475a"
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(root.cHover.r, root.cHover.g, root.cHover.b,
                       clickArea.containsMouse ? 0.35 : 0)
        Behavior on color { ColorAnimation { duration: 120 } }
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
                // childrenRect 取实际几何（Loader 采纳 mini 的显式 width 后即为准）
                width: vendorWidget.childrenRect.width + 4; height: 40

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
                            if (mode === "go") return goBarMini
                            if (mode === "plan") return planBarMini
                            return balanceBarMini
                        }
                    }
                }
            }
        }

        // 兜底：无厂商。init 完成后 summary 已就位（未配置密钥/无已启用厂商），
        // 仅在 init 前的短暂窗口（summary 未到）显示"初始化中..."
        Text {
            text: {
                var pv = plugin ? plugin.values : null
                var ids = (pv && pv.vendor_ids) ? pv.vendor_ids : []
                if (ids.length > 0) return ""
                return (pv && pv.summary) ? pv.summary : "初始化中..."
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
            // 显式 width：Loader 无显式尺寸时采纳 item 的 width；不给的话
            // positioner 的 implicit 链会把本组件宽度算丢（子项 implicit 语义不可靠），
            // 导致内容溢出槽位与相邻插件重叠
            width: Math.max(todayText.implicitWidth, balanceText.implicitWidth)
            Text {
                id: todayText
                text: "今日 " + (root.vval(vid, "today") || "...")
                color: root.cSuccess; font.pixelSize: 10
            }
            Text {
                id: balanceText
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
            // 显式 width（= 标签16 + 间距4 + 进度条50 + 间距4 + 百分比28）：
            // Loader 无显式尺寸时采纳 item 的 width；不给会被 implicit 链算丢
            width: 102
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

    // ── Mini Go（三行进度条：滚动/每周/每月）───────────
    // 复用 planBarMini 的 16+4+50+4+28 行式布局，标签用单字
    Component {
        id: goBarMini
        Column {
            spacing: 2
            // 显式 width：Loader 无显式尺寸时采纳 item 的 width；不给的话
            // positioner 的 implicit 链会把本组件宽度算丢
            width: 102
            Repeater {
                model: [
                    { "key": "rolling", "label": "滚" },
                    { "key": "weekly", "label": "周" },
                    { "key": "monthly", "label": "月" }
                ]
                delegate: Row {
                    spacing: 4
                    Text { text: modelData.label; color: root.cFg; font.pixelSize: 9; width: 16 }
                    Rectangle {
                        width: 50; height: 10; radius: 5; color: root.cBg
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            height: 10; radius: 5
                            readonly property real pct: {
                                var v = root.vval(vid, "go_" + modelData.key + "_pct")
                                return (v !== undefined) ? Math.min(100, Math.max(0, Number(v))) : 0
                            }
                            width: Math.max(0, 50 * pct / 100)
                            color: pct < 50 ? root.cSuccess : pct < 80 ? root.cWarning : root.cDanger
                        }
                    }
                    Text {
                        text: {
                            var v = root.vval(vid, "go_" + modelData.key + "_pct")
                            return (v !== undefined ? v : 0) + "%"
                        }
                        color: root.cMuted; font.pixelSize: 9; width: 28
                    }
                }
            }
        }
    }

    // ── Bar 点击 → 切换弹窗 ──────────────────────────────────────
    // 点 bar 任意处关闭弹窗：bar 是 WindowDoesNotAcceptFocus，点击不会让弹窗
    // 失活（onActiveChanged 不触发），靠宿主 barWindow.barPressed 信号补齐；
    // 点桌面/其他窗口仍走失活关闭。信号先于 item 的 onClicked 到达，
    // 用 popupOpenOnBarPress 记录按下瞬间弹窗状态，避免「点部件关弹窗」
    // 被 onClicked 当成 toggle 又立刻重开。
    property bool popupOpenOnBarPress: false

    Connections {
        target: typeof barWindow !== "undefined" ? barWindow : null
        function onBarPressed() {
            root.popupOpenOnBarPress = popupWin.visible
            popupWin.hide()
            tipDelay.stop(); tip.hide()
        }
    }

    MouseArea {
        id: clickArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: {
            if (root.popupOpenOnBarPress) {
                // 本次按下已经由 barPressed 把弹窗关掉了，不再重开
                root.popupOpenOnBarPress = false
                return
            }
            root.openPopup()
        }
        onContainsMouseChanged: {
            if (containsMouse) tipDelay.restart()
            else { tipDelay.stop(); tip.hide() }
        }
    }

    // ── 悬停提示气泡（400ms 延迟） ──────────────────────────────
    Timer { id: tipDelay; interval: 400; onTriggered: root.showTip() }

    // 定位一律用 mapToGlobal（真实屏幕坐标）：taskbar 模式下 bar 是任务栏子窗口，
    // Qt 缓存的 Window.window.x/y 是父客户区坐标（不可靠）；上下翻转也不看
    // barConfig.barEdge（taskbar 模式下它与任务栏实际位置无关），统一 show 后
    // 按真实屏幕几何越界修正（doClamp）。
    function showTip() {
        const anchor = root.mapToGlobal(Qt.point(root.width / 2, 0))
        tip.x = Math.round(anchor.x - tip.width / 2)
        tip.y = Math.round(anchor.y + root.height + 6)
        tip.pendingClamp = true   // 必须在 show() 前置位：onVisibleChanged 同步触发
        tip.show()
    }

    Window {
        id: tip; visible: false
        property bool pendingClamp: false
        width: tipText.implicitWidth + 16; height: tipText.implicitHeight + 10
        color: "transparent"
        flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

        onVisibleChanged: if (visible && pendingClamp) Qt.callLater(tipDoClamp)

        function tipDoClamp() {
            pendingClamp = false
            // 同 popupWin.doClamp：屏幕几何取宿主注入的 barScreenGeometry
            const sg = (typeof barScreenGeometry !== "undefined")
                       ? barScreenGeometry : null
            if (!sg || sg.width === 0) return
            const sRight = sg.x + sg.width; const sBottom = sg.y + sg.height
            let x = tip.x; let y = tip.y
            if (x + tip.width > sRight)  x = Math.round(sRight - tip.width)
            if (x < sg.x)                x = sg.x
            if (y + tip.height > sBottom) {
                const abovePt = root.mapToGlobal(Qt.point(root.width / 2, -6))
                y = Math.round(abovePt.y - tip.height)
            }
            if (y < sg.y) y = sg.y
            tip.x = x; tip.y = y
        }
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
        popupWin.x = Math.round(anchor.x - pw / 2)
        popupWin.y = Math.round(anchor.y)
        // 清掉上次上翻遗留的底边锚：否则卡片重建的高度瞬变会让 onHeightChanged
        // 用旧锚抢跑，doClamp 看到 y 已被预调而误判"不越界"不上翻（实测导致
        // 弹窗位置 正常/偏高 逐次交替）
        popupWin.bottomAnchor = -1
        // 隐身创建：定位是「先 show 再 clamp」两段式（高度要等卡片布局才收敛），
        // 不隐身用户会看到弹窗从屏幕外跳到正确位置；doClamp 末尾恢复 opacity
        popupWin.opacity = 0
        popupWin.pendingClamp = true   // 必须在 show() 前置位：onVisibleChanged 同步触发
        rebuildCards()                 // 提前重建，让高度尽量在 show 前收敛
        popupWin.show()
        popupWin.raise()
        popupWin.requestActivate()
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
            let comp = balanceCardComp
            if (mode === "plan") comp = planCardComp
            else if (mode === "go") comp = goCardComp
            const obj = comp.createObject(cardContainer, {
                "vendorId": vid
            })
            if (obj) obj.Layout.fillWidth = true
        }
    }

    Window {
        id: popupWin
        property bool pendingClamp: false
        // >=0 时为「上翻」模式：弹窗底边锚定在该屏幕 y 处（部件上方 6px）。
        // 卡片是 show 后异步填充的，高度会在 doClamp 之后继续变化（实测 520→394），
        // 只 clamp 一次会让弹窗悬空——锚定底边后由 onHeightChanged 持续跟随。
        property real bottomAnchor: -1

        visible: false; width: 400
        height: Math.min(520, Math.max(200, cardContainer.implicitHeight + 100))
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        onActiveChanged: if (!active && visible) hide()
        Shortcut { sequence: "Escape"; onActivated: popupWin.hide() }
        onVisibleChanged: if (visible && pendingClamp) Qt.callLater(doClamp)
        onHeightChanged: {
            if (visible && bottomAnchor >= 0)
                y = Math.round(bottomAnchor - height)
            // 高度每变一次都把显现时间后推：等高度稳定 50ms 再显示，
            // 否则用户会看到 doClamp 之后高度继续收敛带来的顶边收缩
            if (visible) revealTimer.restart()
        }

        // 显现定时器：doClamp 或最后一次高度变化 50ms 后恢复 opacity
        Timer {
            id: revealTimer; interval: 50
            onTriggered: popupWin.opacity = 1
        }

        function doClamp() {
            pendingClamp = false
            // 屏幕几何取宿主注入的 barScreenGeometry（当前屏 availableGeometry，
            // 已排除任务栏占位）：弹窗先开在屏幕外时其内部 Item 的 Screen 无效；
            // taskbar 模式下 bar 是任务栏子窗口，root.Screen 同样失效（实测 undefined）。
            // 注意 QML rect 只有 x/y/width/height，right/bottom 要自己算。
            const sg = (typeof barScreenGeometry !== "undefined")
                       ? barScreenGeometry : null
            if (!sg || sg.width === 0) { popupWin.opacity = 1; return }
            const sRight = sg.x + sg.width; const sBottom = sg.y + sg.height
            const w = popupWin.width; const h = popupWin.height
            // y 不读当前值（onHeightChanged 可能在高度瞬变时改过它），
            // 从锚点重算「下方展开」的目标位置再判断是否越界
            let x = popupWin.x
            const belowPt = root.mapToGlobal(Qt.point(root.width / 2, root.height + 6))
            let y = Math.round(belowPt.y)
            if (x + w > sRight)  x = Math.round(sRight - w)
            if (x < sg.x)        x = sg.x
            if (y + h > sBottom) {
                const abovePt = root.mapToGlobal(Qt.point(root.width / 2, -6))
                bottomAnchor = Math.round(abovePt.y)
                y = Math.round(abovePt.y - h)
            }
            if (y < sg.y) y = sg.y
            popupWin.x = x; popupWin.y = y
            revealTimer.restart()   // 高度可能继续瞬变，由定时器在最后稳定后显现
        }

        // ── 内容 ─────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            color: root.cBg; radius: 8
            border.width: 1; border.color: root.cBorder

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "用量中心"; color: root.cFg; font.pixelSize: 15; font.bold: true }
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

    // ── OpenCode Go 卡片 ────────────────────────────────────────
    // 三个窗口（滚动/每周/每月）结构相同，用 Repeater + goWindowBox 渲染；
    // 窗口定义数组只存 key 与标题，其余字段按 go_<key>_* 约定从 values 取
    Component {
        id: goCardComp
        Rectangle {
            id: goCard
            property string vendorId: ""

            Layout.fillWidth: true
            implicitHeight: goCol.implicitHeight + 20
            radius: 8; color: root.cPanel
            border.width: 1; border.color: root.cBorder

            ColumnLayout {
                id: goCol; anchors.fill: parent; anchors.margins: 10; spacing: 6

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        implicitWidth: 8; implicitHeight: 8; radius: 4
                        color: root.vendorVal(goCard.vendorId, "status") === "ok" ? root.cSuccess
                             : root.vendorVal(goCard.vendorId, "status") === "error" ? root.cDanger
                             : root.cWarning
                    }
                    Text {
                        text: root.vendorVal(goCard.vendorId, "name") || goCard.vendorId
                        color: root.cFg; font.pixelSize: 13; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: "Go"; color: root.cMuted; font.pixelSize: 10 }
                }

                // 数据区
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: {
                        const st = root.vendorVal(goCard.vendorId, "status")
                        return st === "ok" ? goDataOk : goDataErr
                    }
                }

                Component {
                    id: goDataErr
                    Text {
                        text: {
                            const st = root.vendorVal(goCard.vendorId, "status")
                            if (st === "error") return "错误: " + (root.vendorVal(goCard.vendorId, "error") || "未知")
                            return "查询中..."
                        }
                        color: root.vendorVal(goCard.vendorId, "status") === "error" ? root.cDanger : root.cMuted
                        font.pixelSize: 11
                    }
                }

                Component {
                    id: goDataOk
                    ColumnLayout {
                        spacing: 8

                        // 订阅状态 + Zen 余额兜底（页面级全局信息）
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text {
                                text: root.vendorVal(goCard.vendorId, "go_subscribed") ? "已订阅 OpenCode Go" : "未订阅"
                                color: root.vendorVal(goCard.vendorId, "go_subscribed") ? root.cSuccess : root.cMuted
                                font.pixelSize: 11; font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Zen 余额兜底: " + (root.vendorVal(goCard.vendorId, "go_use_balance") ? "开启" : "关闭")
                                color: root.cMuted; font.pixelSize: 10
                            }
                        }

                        // 三个窗口
                        Repeater {
                            model: [
                                { "key": "rolling", "title": "滚动窗口" },
                                { "key": "weekly", "title": "每周窗口" },
                                { "key": "monthly", "title": "每月窗口" }
                            ]
                            delegate: goWindowBox
                        }
                    }
                }

                // 单个窗口块：标题 + 用量金额 + 重置倒计时 + 进度条
                Component {
                    id: goWindowBox
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: goWinCol.implicitHeight + 12
                        radius: 6; color: root.cBg
                        border.width: 1; border.color: root.cBorder

                        ColumnLayout {
                            id: goWinCol
                            anchors.fill: parent; anchors.margins: 8; spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.title
                                    color: root.cFg; font.pixelSize: 11; font.bold: true
                                }
                                Text {
                                    text: root.vendorVal(goCard.vendorId, "go_" + modelData.key + "_detail") || "-"
                                    color: root.cMuted; font.pixelSize: 10
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "重置: " + (root.vendorVal(goCard.vendorId, "go_" + modelData.key + "_reset") || "-")
                                    color: root.cMuted; font.pixelSize: 10
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 6

                                Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 14
                                    radius: 7; color: root.cPanel

                                    Rectangle {
                                        id: goWinBar
                                        anchors.left: parent.left
                                        anchors.top: parent.top; anchors.bottom: parent.bottom
                                        radius: 7
                                        readonly property real pct: {
                                            var v = root.vendorVal(goCard.vendorId, "go_" + modelData.key + "_pct")
                                            return (v !== undefined && v !== null) ? Math.min(100, Math.max(0, Number(v))) : 0
                                        }
                                        width: Math.max(goWinBar.radius * 2, parent.width * goWinBar.pct / 100)
                                        color: goWinBar.pct < 50 ? root.cSuccess : goWinBar.pct < 80 ? root.cWarning : root.cDanger
                                    }
                                }

                                Text {
                                    text: (root.vendorVal(goCard.vendorId, "go_" + modelData.key + "_pct") || 0) + "%"
                                    color: root.cFg; font.pixelSize: 12; font.bold: true
                                    Layout.preferredWidth: 35
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "更新: " + (root.vendorVal(goCard.vendorId, "last_update") || "...")
                        color: root.cMuted; font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "⟳"; color: root.cAccent; font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (plugin) plugin.call("refresh_vendor", [goCard.vendorId])
                                Qt.callLater(rebuildCards)
                            }
                        }
                    }
                }
            }
        }
    }
}
