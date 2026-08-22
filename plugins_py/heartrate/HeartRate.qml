// HeartRate.qml — py_heartrate 子插件界面：bleak 心率显示
// 契约同 C++ 插件：根 Item 声明 property var plugin；数据走 plugin.values，
// 行为走 plugin.call(name, args) → main.py 的 invoke()。
// 弹窗范式照抄 usage_hub：mapToGlobal 锚点 + barScreenGeometry 越界修正 +
// 隐身创建/稳定后显现 + barWindow.barPressed 关窗守卫。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var plugin
    function trText(source) { return qsTranslate("plugin.py_heartrate.main", source) }

    function errorText() {
        if (!plugin || !plugin.values) return ""
        var code = plugin.values.errorCode
        var text = code === "bleakMissing" ? trText("未安装 bleak，请运行: py -m pip install bleak")
                 : code === "scanFailed" ? trText("扫描失败")
                 : code === "connectFailed" ? trText("连接失败") : ""
        var detail = plugin.values.errorDetail
        return text + (detail ? ": " + detail : "")
    }

    // 宽度跟随内容：未连接时显示"未连接设备"，连接后显示"♥ 72"，写死宽度会被槽位 clip:true 截断
    width: barText.implicitWidth + 24
    height: 40

    // 色板别名：统一从 themeManager.colors 取（缺失时给深色兜底）
    readonly property var colors: themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "success": "#a6e3a1", "warning": "#f9e2af",
        "hover": "#45475a", "border": "#45475a", "onAccent": "#1e1e2e"
    })

    // 状态速记（供 bar 文案/颜色/弹窗复用）
    readonly property string state: plugin && plugin.values ? String(plugin.values.state) : "idle"
    readonly property bool connected: root.state === "connected"

    // ── Bar 点击 → 切换弹窗 ──────────────────────────────────────
    // 点 bar 任意处关闭弹窗：bar 是 WindowDoesNotAcceptFocus，点击不会让弹窗
    // 失活（onActiveChanged 不触发），靠宿主 barWindow.barPressed 信号补齐。
    // 信号先于 item 的 onClicked 到达，用 popupOpenOnBarPress 记录按下瞬间
    // 弹窗状态，避免「点部件关弹窗」被 onClicked 当成 toggle 又立刻重开。
    property bool popupOpenOnBarPress: false
    // 曲线时长窗口选中态（打开弹窗时从 plugin.values.historyWindow 同步）
    property int winSel: 5

    Connections {
        target: typeof barWindow !== "undefined" ? barWindow : null
        function onBarPressed() {
            root.popupOpenOnBarPress = popupWin.visible
            popupWin.hide()
        }
    }

    // bar hover 反馈（提示可点击）
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(root.colors.hover.r, root.colors.hover.g, root.colors.hover.b,
                       clickArea.containsMouse ? 0.35 : 0)
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Text {
        id: barText
        anchors.centerIn: parent
        font.pixelSize: 15
        // 文案映射：连接+有数据 → "♥ 72"；连接中 → "连接中…"；其余 → "未连接设备"
        text: {
            if (!plugin || !plugin.values) return root.trText("未连接设备")
            const s = String(plugin.values.state)
            if (s === "connecting") return root.trText("连接中…")
            if (s === "connected") {
                const h = plugin.values.hr
                if (h !== null && h !== undefined) return "♥ " + h
                return root.trText("已连接")
            }
            return root.trText("未连接设备")
        }
        // 有心率时高亮（accent），否则 muted
        color: {
            if (!plugin || !plugin.values) return root.colors.muted
            const s = String(plugin.values.state)
            if (s === "connected" && plugin.values.hr !== null && plugin.values.hr !== undefined)
                return root.colors.accent
            return root.colors.muted
        }
    }

    MouseArea {
        id: clickArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (root.popupOpenOnBarPress) {
                // 本次按下已经由 barPressed 把弹窗关掉了，不再重开
                root.popupOpenOnBarPress = false
                return
            }
            root.openPopup()
        }
    }

    // ── 弹出窗口 ─────────────────────────────────────────────────
    // 常用设备相对时间（lastSeen 秒级时间戳）
    function relTime(ts) {
        if (!ts) return ""
        const s = Math.floor(Date.now() / 1000) - ts
        if (s < 60) return trText("刚刚")
        if (s < 3600) return trText("%1 分钟前").arg(Math.floor(s / 60))
        if (s < 86400) return trText("%1 小时前").arg(Math.floor(s / 3600))
        return trText("%1 天前").arg(Math.floor(s / 86400))
    }

    function openPopup() {
        const pw = 360
        const anchor = root.mapToGlobal(Qt.point(root.width / 2, root.height + 6))
        popupWin.x = Math.round(anchor.x - pw / 2)
        popupWin.y = Math.round(anchor.y)
        // 清掉上次上翻遗留的底边锚：否则高度瞬变会让 onHeightChanged 用旧锚抢跑
        popupWin.bottomAnchor = -1
        // 隐身创建：定位是「先 show 再 clamp」两段式，不隐身用户会看到弹窗
        // 从屏幕外跳到正确位置；doClamp 末尾由 revealTimer 恢复 opacity
        popupWin.opacity = 0
        popupWin.pendingClamp = true   // 必须在 show() 前置位：onVisibleChanged 同步触发
        popupWin.show()
        popupWin.raise()
        popupWin.requestActivate()
    }

    Window {
        id: popupWin
        transientParent: null
        property bool pendingClamp: false
        // >=0 时为「上翻」模式：弹窗底边锚定在该屏幕 y 处（部件上方 6px）。
        property real bottomAnchor: -1

        visible: false; width: 360
        height: Math.min(640, Math.max(320, contentCol.implicitHeight + 24))
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        onActiveChanged: if (!active && visible) hide()
        Shortcut { sequence: "Escape"; onActivated: popupWin.hide() }
        onVisibleChanged: {
            if (visible) {
                if (pendingClamp) Qt.callLater(doClamp)
                // 弹窗打开即扫描、关闭即停扫：扫描只在需要时占用蓝牙无线电
                if (plugin) plugin.call("startScan", [])
                // 历史曲线：打开才向 values 输出数据；同步上次窗口选中态
                if (plugin) {
                    if (plugin.values && plugin.values.historyWindow)
                        root.winSel = plugin.values.historyWindow
                    plugin.call("setChartActive", [true])
                }
            } else {
                if (plugin) plugin.call("stopScan", [])
                if (plugin) plugin.call("setChartActive", [false])
            }
        }
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
            // 已排除任务栏占位）：taskbar 模式下 bar 是任务栏子窗口，
            // root.Screen 同样失效（实测 undefined）。rect 只有 x/y/width/height。
            const sg = (typeof barScreenGeometry !== "undefined")
                       ? barScreenGeometry : null
            if (!sg || sg.width === 0) { popupWin.opacity = 1; return }
            const sRight = sg.x + sg.width; const sBottom = sg.y + sg.height
            const w = popupWin.width; const h = popupWin.height
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
            revealTimer.restart()
        }

        // ── 内容 ─────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            color: root.colors.bg; radius: 8
            border.width: 1; border.color: root.colors.border

            ColumnLayout {
                id: contentCol
                anchors.fill: parent; anchors.margins: 12; spacing: 8

                // 标题行
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: root.trText("心率设备"); color: root.colors.fg; font.pixelSize: 15; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "✕"; color: root.colors.muted; font.pixelSize: 16
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: popupWin.hide()
                        }
                    }
                }

                // 状态行：当前状态 + 错误信息（红色）
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text {
                        Layout.fillWidth: true
                        color: root.colors.fg; font.pixelSize: 12
                        text: {
                            if (!plugin || !plugin.values) return ""
                            const pv = plugin.values
                            if (pv.state === "connected") {
                                const h = pv.hr
                                const hrTxt = (h !== null && h !== undefined) ? "♥ " + h + " bpm" : root.trText("等待数据…")
                                return root.trText("已连接") + " " + (pv.deviceName || "") + "　" + hrTxt
                            }
                            if (pv.state === "connecting") return root.trText("连接中") + " " + (pv.deviceName || "") + "…"
                            if (pv.state === "scanning") return root.trText("正在扫描附近设备…")
                            if (pv.state === "error") return root.trText("出错")
                            return root.trText("未连接设备")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        color: root.colors.danger; font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        visible: root.errorText().length > 0
                        text: root.errorText()
                    }
                }

                // 历史曲线：窗口分段按钮 + Canvas 自绘折线
                // （不用 QtCharts：宿主 windeployqt 未部署该模块，插件保持零宿主依赖）
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Text {
                            color: root.colors.muted; font.pixelSize: 11
                            text: root.trText("历史曲线")
                        }
                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: [1, 3, 5, 10, 30]
                            Rectangle {
                                required property int modelData
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 22
                                radius: 4
                                color: root.winSel === modelData
                                       ? root.colors.accent
                                       : (winBtnMouse.containsMouse ? root.colors.hover : root.colors.panel)
                                border.width: 1
                                border.color: root.winSel === modelData
                                              ? root.colors.accent : root.colors.border
                                Text {
                                    anchors.centerIn: parent
                                    color: root.winSel === modelData ? root.colors.onAccent : root.colors.fg
                                    font.pixelSize: 11
                                    text: root.trText("%1 分").arg(modelData)
                                }
                                MouseArea {
                                    id: winBtnMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.winSel = modelData
                                        if (plugin) plugin.call("setWindow", [modelData])
                                    }
                                }
                            }
                        }
                    }
                    Canvas {
                        id: chart
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        property var series: plugin && plugin.values ? (plugin.values.history || []) : []
                        onSeriesChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = chart.getContext("2d")
                            const w = chart.width, h = chart.height
                            ctx.clearRect(0, 0, w, h)
                            ctx.fillStyle = root.colors.panel
                            ctx.fillRect(0, 0, w, h)
                            const pts = chart.series
                            if (!pts || pts.length < 2) {
                                ctx.fillStyle = root.colors.muted
                                ctx.font = "11px sans-serif"
                                ctx.textAlign = "center"
                                ctx.textBaseline = "middle"
                                ctx.fillText(root.trText("暂无数据"), w / 2, h / 2)
                                return
                            }
                            // 数值范围（含上下留白）
                            let mn = Infinity, mx = -Infinity
                            for (let i = 0; i < pts.length; i++) {
                                const v = pts[i][1]
                                if (v < mn) mn = v
                                if (v > mx) mx = v
                            }
                            if (mn === mx) { mn -= 3; mx += 3 }
                            const padY = 12
                            const yOf = v => h - padY - (v - mn) / (mx - mn) * (h - 2 * padY)
                            // 三条水平网格线（max / mid / min）+ 极值标注
                            ctx.strokeStyle = root.colors.border
                            ctx.lineWidth = 1
                            ctx.font = "9px sans-serif"
                            ctx.textAlign = "left"
                            ctx.textBaseline = "top"
                            const ticks = [mx, (mx + mn) / 2, mn]
                            for (let i = 0; i < ticks.length; i++) {
                                const y = yOf(ticks[i])
                                ctx.beginPath()
                                ctx.moveTo(0, y); ctx.lineTo(w, y)
                                ctx.stroke()
                                ctx.fillStyle = root.colors.muted
                                ctx.fillText(Math.round(ticks[i]) + "", 3, y + 1)
                            }
                            // 折线
                            ctx.strokeStyle = root.colors.accent
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            for (let i = 0; i < pts.length; i++) {
                                const x = i / (pts.length - 1) * w
                                const y = yOf(pts[i][1])
                                if (i === 0) ctx.moveTo(x, y)
                                else ctx.lineTo(x, y)
                            }
                            ctx.stroke()
                            // 当前值标注（最后一点）
                            const last = pts[pts.length - 1]
                            ctx.fillStyle = root.colors.accent
                            ctx.font = "bold 11px sans-serif"
                            ctx.textAlign = "right"
                            ctx.textBaseline = "bottom"
                            ctx.fillText(last[1] + "", w - 4, yOf(last[1]) - 2)
                        }
                    }
                    // 统计行：当前窗口最大值 / 最小值 / 平均值
                    Text {
                        Layout.fillWidth: true
                        visible: plugin && plugin.values
                                 && plugin.values.historyMax !== null
                                 && plugin.values.historyMax !== undefined
                        horizontalAlignment: Text.AlignHCenter
                        color: root.colors.muted
                        font.pixelSize: 11
                        text: root.trText("最大 %1　最小 %2　平均 %3")
                                  .arg(plugin.values.historyMax)
                                  .arg(plugin.values.historyMin)
                                  .arg(plugin.values.historyAvg)
                    }
                }

                // 设备列表：常用设备（记忆的历史连接，带相对时间）在前，
                // 扫描结果（带 RSSI）在后，同一个滚动列表。
                // 连接成功后不再搜索，本区切换为已连接设备行。
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(300, Math.max(120, devList.contentHeight + 8))
                    radius: 6
                    color: root.colors.panel
                    visible: root.state !== "connected"

                    ListView {
                        id: devList
                        anchors.fill: parent; anchors.margins: 4
                        clip: true
                        spacing: 2
                        model: plugin && plugin.values
                               ? (plugin.values.knownDevices || []).concat(plugin.values.devices || [])
                               : []
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            width: 4
                            background: Rectangle { color: "transparent" }
                        }
                        delegate: Rectangle {
                            id: devRow
                            required property var model
                            width: devList.width
                            height: 34
                            radius: 4
                            readonly property bool isTarget: plugin && plugin.values
                                    && String(plugin.values.deviceId) === String(devRow.model.id)
                                    && (String(plugin.values.state) === "connected"
                                        || String(plugin.values.state) === "connecting")
                            color: {
                                if (devRow.isTarget) return root.colors.accent
                                if (rowMouse.containsMouse) return root.colors.hover
                                return "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                spacing: 6
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: root.colors.fg; font.pixelSize: 12
                                    text: devRow.model.name || root.trText("未命名设备")
                                }
                                Text {
                                    // 常用设备显示相对时间，扫描结果显示信号强度
                                    color: root.colors.muted; font.pixelSize: 10
                                    text: devRow.model.lastSeen
                                          ? root.relTime(devRow.model.lastSeen)
                                          : (devRow.model.rssi !== null && devRow.model.rssi !== undefined
                                             ? devRow.model.rssi + " dBm" : "")
                                }
                                Text {
                                    visible: devRow.isTarget
                                    color: root.colors.onAccent; font.pixelSize: 11; font.bold: true
                                    text: String(plugin.values.state) === "connected"
                                          ? root.trText("已连接") : root.trText("连接中")
                                }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const pv = plugin ? plugin.values : null
                                    if (!pv) return
                                    const busy = pv.state === "connecting"
                                    if (!busy && plugin) plugin.call("connect", [devRow.model.id, devRow.model.name || ""])
                                }
                            }
                        }
                    }
                }

                // 已连接设备行：连接成功后不再搜索，本区显示当前设备
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: 6
                    color: root.colors.panel
                    visible: root.state === "connected"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: root.colors.fg; font.pixelSize: 12
                            text: plugin && plugin.values ? (plugin.values.deviceName || "") : ""
                        }
                        Text {
                            color: root.colors.success; font.pixelSize: 11; font.bold: true
                            text: root.trText("已连接")
                        }
                    }
                }

                // 空列表提示（合并常用 + 扫描结果后仍为空才显示）
                Text {
                    Layout.fillWidth: true
                    visible: {
                        if (!plugin || !plugin.values) return true
                        const kn = plugin.values.knownDevices || []
                        const dv = plugin.values.devices || []
                        return kn.length + dv.length === 0
                    }
                    color: root.colors.muted; font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    text: {
                        if (!plugin || !plugin.values) return ""
                        const s = String(plugin.values.state)
                        if (s === "error") return root.trText("点击右上角 ✕ 关闭")
                        if (s === "scanning") return root.trText("扫描中…未发现设备（确认设备已开启广播）")
                        if (s === "connected" || s === "connecting") return ""
                        return root.trText("未连接设备")
                    }
                }

                // 底部操作行
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        id: scanBtn
                        Layout.preferredHeight: 26
                        width: 88
                        radius: 4
                        visible: root.state !== "connected"
                        color: scanBtnMouse.containsMouse ? root.colors.hover : root.colors.panel
                        border.width: 1; border.color: root.colors.border
                        Text {
                            anchors.centerIn: parent
                            color: root.colors.fg; font.pixelSize: 12
                            text: {
                                const s = plugin && plugin.values ? String(plugin.values.state) : "idle"
                                return s === "scanning" ? root.trText("重新扫描") : root.trText("扫描")
                            }
                        }
                        MouseArea {
                            id: scanBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // 清空旧结果重扫：扫描是持续的，重新扫描 = 重启扫描任务
                            onClicked: if (plugin) {
                                plugin.call("stopScan", [])
                                plugin.call("startScan", [])
                            }
                        }
                    }
                    Rectangle {
                        id: discBtn
                        Layout.preferredHeight: 26
                        width: 76
                        radius: 4
                        visible: root.connected
                        color: discBtnMouse.containsMouse ? root.colors.hover : root.colors.panel
                        border.width: 1; border.color: root.colors.danger
                        Text {
                            anchors.centerIn: parent
                            color: root.colors.danger; font.pixelSize: 12
                            text: root.trText("断开连接")
                        }
                        MouseArea {
                            id: discBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (plugin) plugin.call("disconnect", [])
                        }
                    }
                }
            }
        }
    }
}
