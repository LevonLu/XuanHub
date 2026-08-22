// Todo.qml — py_todo 子插件界面（Bar 部件 + 弹出窗口）
// Bar: 显示待办摘要（数量模式 / 最近一项模式），点击弹出弹窗。
// Popup: 待办列表弹窗，支持添加 / 勾选完成 / 双击编辑 / 删除 / 清空已完成，
// 数据经 plugin.call(name, args) → main.py invoke → sqlite，框架自动重拉 values。
// 弹窗定位/越界修正/barPressed 收听后关闭整套范式照抄 UsageHub.qml（勿简化：
// 隐身创建 + doClamp + 底边锚定缺一不可，taskbar 模式下 Item.Screen 失效）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import XuanHub

Item {
    id: root
    property var plugin
    function trText(source) { return qsTranslate("plugin.py_todo.main", source) }

    // childrenRect 取实际几何（不走 implicit 链——positioner/Rectangle 的 implicit 语义不可靠）
    width: Math.max(120, barRow.childrenRect.width + 16)
    height: 40

    // 色板兜底
    readonly property color cFg:      themeManager ? themeManager.colors.fg : "#cdd6f4"
    readonly property color cMuted:   themeManager ? themeManager.colors.muted : "#7f849c"
    readonly property color cSuccess: themeManager ? themeManager.colors.success : "#a6e3a1"
    readonly property color cDanger:  themeManager ? themeManager.colors.danger : "#f38ba8"
    readonly property color cPanel:   themeManager ? themeManager.colors.panel : "#313244"
    readonly property color cBorder:  themeManager ? themeManager.colors.border : "#45475a"
    readonly property color cBg:      themeManager ? themeManager.colors.bg : "#1e1e2e"
    readonly property color cAccent:  themeManager ? themeManager.colors.accent : "#89b4fa"
    property color cHover: themeManager ? themeManager.colors.hover : "#45475a"

    // 辅助：从 plugin.values 读取
    function vv(key) {
        var pv = plugin ? plugin.values : null
        return pv ? pv[key] : undefined
    }
    // 无状态 delegate 的数据入口：按 id 现查（Repeater 按索引复用 delegate 时
    // modelData 会更新为别的行的 id，绑定经这里重新求值，杜绝文本串行）
    function todoText(id) {
        var todos = vv("todos")
        if (!todos) return ""
        for (var i = 0; i < todos.length; i++)
            if (todos[i].id === id) return todos[i].text
        return ""
    }
    function todoDone(id) {
        var todos = vv("todos")
        if (!todos) return false
        for (var i = 0; i < todos.length; i++)
            if (todos[i].id === id) return todos[i].done
        return false
    }

    // ── Bar 内容 ──────────────────────────────────────────────
    Row {
        id: barRow
        anchors.centerIn: parent
        spacing: 6

        Text {
            text: {
                var pv = plugin ? plugin.values : null
                if (!pv) return "…"
                var prefix = pv.prefix !== undefined ? pv.prefix : "📝"
                if (pv.barMode === "latest" || pv.barMode === "最近一项") {
                    var fa = pv.firstActive
                    return fa ? prefix + " " + fa : prefix + " " + root.trText("无待办")
                }
                var n = pv.activeCount !== undefined ? pv.activeCount : 0
                return prefix + " " + root.trText("%1 项").arg(n)
            }
            // 显式宽度上限：最近一项模式长文本截断，bar 部件宽度因此受限
            readonly property real maxW: 260
            width: Math.min(maxW, implicitWidth)
            elide: Text.ElideRight
            color: root.cFg
            font.pixelSize: 13
            font.bold: true
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // bar hover 反馈（提示部件可点击弹窗）
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(root.cHover.r, root.cHover.g, root.cHover.b,
                       clickArea.containsMouse ? 0.35 : 0)
        Behavior on color { ColorAnimation { duration: 120 } }
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
    }

    // ── 弹出窗口 ─────────────────────────────────────────────────
    function openPopup() {
        const pw = popupWin.width
        const anchor = root.mapToGlobal(Qt.point(root.width / 2, root.height + 6))
        popupWin.x = Math.round(anchor.x - pw / 2)
        popupWin.y = Math.round(anchor.y)
        // 清掉上次上翻遗留的底边锚：否则卡片重建的高度瞬变会让 onHeightChanged
        // 用旧锚抢跑，doClamp 看到 y 已被预调而误判"不越界"不上翻
        popupWin.bottomAnchor = -1
        // 隐身创建：定位是「先 show 再 clamp」两段式（高度要等列表布局才收敛），
        // 不隐身用户会看到弹窗从屏幕外跳到正确位置；doClamp 末尾恢复 opacity
        popupWin.opacity = 0
        popupWin.pendingClamp = true   // 必须在 show() 前置位：onVisibleChanged 同步触发
        popupWin.show()
        popupWin.raise()
        popupWin.requestActivate()
    }

    // 编辑态：当前正在编辑的条目 id（-1 = 无）。切换后行内 TextField 可见
    property int editingId: -1

    function addCurrent() {
        var t = addField.text.trim()
        if (t.length > 0) {
            if (plugin) plugin.call("add", [t])
            addField.text = ""
        }
    }

    function commitEdit(id, text) {
        var t = (text || "").trim()
        if (t.length > 0) {
            if (plugin) plugin.call("edit", [id, t])
        }
        // 空文本视为取消编辑；删除/清空后编辑 id 已不存在，这里统一复位
        editingId = -1
    }

    Window {
        id: popupWin
        transientParent: null
        property bool pendingClamp: false
        // >=0 时为「上翻」模式：弹窗底边锚定在该屏幕 y 处（部件上方 6px）。
        // 列表是异步填充的，高度会在 doClamp 之后继续变化（增删条目），
        // 只 clamp 一次会让弹窗悬空——锚定底边后由 onHeightChanged 持续跟随。
        property real bottomAnchor: -1

        visible: false; width: 420
        height: Math.min(480, Math.max(240, listCol.implicitHeight + 180))
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
                    Text {
                        text: root.trText("待办事项"); color: root.cFg
                        font.pixelSize: 15; font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: {
                            var c = root.vv("activeCount")
                            var t = root.vv("count")
                            return (c !== undefined ? c : 0) + " / " + (t !== undefined ? t : 0)
                        }
                        color: root.cMuted; font.pixelSize: 11
                    }
                    Text {
                        text: "✕"; color: root.cMuted; font.pixelSize: 16
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: popupWin.hide()
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                // 添加行
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: addField
                        Layout.fillWidth: true
                        placeholderText: root.trText("输入新待办，回车添加")
                        selectByMouse: true
                        onAccepted: root.addCurrent()
                    }
                    Rectangle {
                        implicitWidth: addBtnText.implicitWidth + 18
                        implicitHeight: UiMetrics.controlHeightS; radius: UiMetrics.radiusS
                        color: addBtnMa.containsMouse ? root.cBorder : root.cPanel
                        border.width: 1; border.color: root.cBorder
                        Text {
                            id: addBtnText; anchors.centerIn: parent
                            text: "＋ " + root.trText("添加"); color: root.cAccent; font.pixelSize: 11
                        }
                        MouseArea {
                            id: addBtnMa; anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: root.addCurrent()
                        }
                    }
                }

                // 列表（滚动）
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded; clip: true

                    ColumnLayout {
                        id: listCol; width: parent.width - 8; spacing: 4

                        Text {
                            id: emptyLabel
                            visible: {
                                var t = root.vv("todos")
                                return !t || t.length === 0
                            }
                            text: root.trText("暂无待办，上方添加一条")
                            color: root.cMuted; font.pixelSize: 12
                            Layout.alignment: Qt.AlignCenter; Layout.topMargin: 40
                        }

                        Repeater {
                            model: root.vv("todos") || []
                            delegate: todoRow
                        }

                        // 错误提示（invoke 失败时 main.py 置 lastError）
                        Text {
                            visible: !!root.vv("lastErrorKey")
                            text: {
                                var key = root.vv("lastErrorKey")
                                var message = key === "operationFailed"
                                    ? root.trText("操作失败，详见日志")
                                    : root.trText("待办数据库不可用")
                                var detail = root.vv("lastErrorDetail")
                                return "⚠ " + message + (detail ? ": " + detail : "")
                            }
                            color: root.cDanger; font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.cBorder }

                // 底部：清空已完成
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        visible: {
                            var d = root.vv("doneCount")
                            return d !== undefined && d > 0
                        }
                        text: "🗑 " + root.trText("清空已完成")
                              + " (" + (root.vv("doneCount") || 0) + ")"
                        color: root.cMuted; font.pixelSize: 11
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.editingId = -1
                                if (plugin) plugin.call("clear_done", [])
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    // ── 列表行（无状态 delegate）──────────────────────────────
    // 显示数据一律经 root.todoText/todoDone 按 id 现查，delegate 自身不持有
    // modelData 之外的持久状态——Repeater 按索引复用 delegate 时 modelData 更新
    // 为别的行 id，绑定重新求值即得新行数据，杜绝「勾完一条下一行串字」。
    Component {
        id: todoRow
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // 完成状态圆钮
            Rectangle {
                Layout.preferredWidth: 20; Layout.preferredHeight: 20
                radius: 10
                color: root.todoDone(modelData.id) ? root.cSuccess : "transparent"
                border.width: 1
                border.color: root.todoDone(modelData.id) ? root.cSuccess : root.cBorder
                Text {
                    text: root.todoDone(modelData.id) ? "✓" : ""
                    color: root.cBg; font.pixelSize: 11
                    anchors.centerIn: parent
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: if (plugin) plugin.call("toggle", [modelData.id])
                }
            }

            // 文本（双击进入编辑）
            Text {
                Layout.fillWidth: true
                text: root.todoText(modelData.id) || ""
                color: root.todoDone(modelData.id) ? root.cMuted : root.cFg
                font.pixelSize: 12
                font.strikeout: root.todoDone(modelData.id)
                elide: Text.ElideRight
                visible: root.editingId !== modelData.id
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onDoubleClicked: root.editingId = modelData.id
                }
            }

            // 编辑框（仅编辑态可见；每次进入取 DB 最新文本并抢焦点）
            TextField {
                id: editField
                Layout.fillWidth: true
                visible: root.editingId === modelData.id
                selectByMouse: true
                onVisibleChanged: {
                    if (visible) {
                        text = root.todoText(modelData.id) || ""
                        forceActiveFocus()
                    }
                }
                onAccepted: root.commitEdit(modelData.id, text)
                Keys.onEscapePressed: root.editingId = -1
            }

            // 编辑入口（✎ 与双击等价）
            Text {
                text: "✎"; color: root.cMuted; font.pixelSize: 12
                visible: root.editingId !== modelData.id
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: root.editingId = modelData.id
                }
            }

            // 删除
            Text {
                text: "✕"; color: root.cMuted; font.pixelSize: 12
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.editingId === modelData.id) root.editingId = -1
                        if (plugin) plugin.call("remove", [modelData.id])
                    }
                }
            }
        }
    }
}
