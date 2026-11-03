// Todo.qml — py_todo 子插件界面（Bar 部件 + 弹出窗口）
// Bar: 显示待办摘要（数量模式 / 最近一项模式），点击弹出弹窗。
// Popup: 待办列表弹窗，支持添加 / 勾选完成 / 双击编辑 / 删除 / 清空已完成，
// 数据经 plugin.call(name, args) → main.py invoke → sqlite，框架自动重拉 values。
// 弹窗使用宿主 PluginPopup，统一处理定位、互斥、失焦、Esc 和 bar toggle 守卫。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    function trText(source) { return qsTranslate("plugin.py_todo.main", source) }

    // childrenRect 取实际几何（不走 implicit 链——positioner/Rectangle 的 implicit 语义不可靠）
    width: Math.max(120, barRow.childrenRect.width + 16)
    height: 40

    // 色板兜底
    readonly property var hostColors: pluginContext ? pluginContext.colors
                                                    : (themeManager ? themeManager.colors : ({}))
    readonly property color cFg:      hostColors.fg || "#ffffff"
    readonly property color cMuted:   hostColors.muted || "#cecece"
    readonly property color cSuccess: hostColors.success || "#6ccb5f"
    readonly property color cDanger:  hostColors.danger || "#ff99a4"
    readonly property color cPanel:   hostColors.panel || "#2b2b2b"
    readonly property color cBorder:  hostColors.border || "#484848"
    readonly property color cBg:      hostColors.bg || "#202020"
    readonly property color cAccent:  hostColors.accent || "#60cdff"
    property color cHover: hostColors.hover || "#383838"

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
    MouseArea {
        id: clickArea
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: popupWin.toggleFromBar()
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

    PluginPopup {
        id: popupWin
        anchorItem: root
        pluginContext: root.pluginContext
        width: 420
        height: Math.min(480, Math.max(240, listCol.implicitHeight + 180))

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
