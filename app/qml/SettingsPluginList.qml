// SettingsPluginList.qml — 按稳定插件 ID 选择、搜索与组内拖拽排序
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: pluginList
    property var colors
    readonly property color foregroundColor: colors.fg
    property var translateText: function(text) { return text }
    property string filterText: ""
    property string draggedId: ""
    property string draggedGroup: ""
    property string draggedName: ""
    property string targetId: ""
    property bool insertAfter: false
    property real insertionY: 0
    property point pointerPosition: Qt.point(0, 0)
    readonly property bool dragging: draggedId.length > 0
    interactive: !dragging
    // 自动滚动不能回收持有鼠标抓取的源行；释放后恢复正常缓存预算。
    cacheBuffer: dragging ? contentHeight : 320
    onFilterTextChanged: cancelDrag()
    onVisibleChanged: { if (!visible) cancelDrag() }
    onActiveFocusChanged: { if (!activeFocus) cancelDrag() }
    Keys.onEscapePressed: function(event) {
        if (dragging) { cancelDrag(); event.accepted = true }
        else event.accepted = false
    }

    function groupLabel(key) {
        const parts = key.split("-")
        const region = parts[1] === "left" ? "左侧区域"
                     : parts[1] === "center" ? "中间区域"
                     : parts[1] === "right" ? "右侧区域" : "未识别区域"
        return translateText(parts[0] === "enabled" ? "已启用" : "未启用")
            + " · " + translateText(region)
    }
    function beginDrag(id) {
        if (filterText.length > 0) return
        const row = model.rowForId(id)
        if (row < 0 || !model.roleValue(row, "enabled")) return
        draggedGroup = model.roleValue(row, "groupKey")
        draggedName = model.roleValue(row, "name") || id
        draggedId = id
    }
    function cancelDrag() {
        draggedId = ""
        targetId = ""
    }
    function updateDrag(point) {
        pointerPosition = point
        targetId = ""
        if (!dragging || point.x < 0 || point.x > width || point.y < 0 || point.y > height) return
        // 命中最近可见行，行间空隙和首尾留白同样有落点；跨分组则不接受。
        let nearest = null
        let distance = Number.MAX_VALUE
        for (let row = 0; row < count; ++row) {
            const item = itemAtIndex(row)
            if (!item || !item.visible) continue
            const top = item.mapToItem(pluginList, 0, 0).y
            if (top + item.height < 0 || top > height) continue
            const gap = Math.max(top - point.y, point.y - top - item.height, 0)
            if (gap < distance) { nearest = item; distance = gap }
        }
        if (!nearest || nearest.groupKey !== draggedGroup || nearest.pluginId === draggedId) return
        const top = nearest.mapToItem(pluginList, 0, 0).y
        targetId = nearest.pluginId
        insertAfter = point.y >= top + nearest.height / 2
        insertionY = Math.max(1, Math.min(height - 3, top + (insertAfter ? nearest.height : 0)))
    }
    function finishDrag() {
        const source = draggedId
        const target = targetId
        const after = insertAfter
        // 先结束手势，再一次性提交草稿，避免代理重排销毁或移动正在抓取鼠标的行。
        cancelDrag()
        if (source.length > 0 && target.length > 0 && model.moveRelativeTo(source, target, after)) {
            currentId = source
            Qt.callLater(function() { positionViewAtIndex(model.rowForId(source), ListView.Contain) })
        }
    }
    Timer {
        interval: 40
        repeat: true
        running: pluginList.dragging
        onTriggered: {
            const p = pluginList.pointerPosition
            if (p.x < 0 || p.x > pluginList.width || p.y < 0 || p.y > pluginList.height) return
            const step = p.y < 32 ? -12 : p.y > pluginList.height - 32 ? 12 : 0
            if (step === 0) return
            pluginList.contentY = Math.max(pluginList.originY,
                Math.min(pluginList.originY + Math.max(0, pluginList.contentHeight - pluginList.height),
                         pluginList.contentY + step))
            pluginList.updateDrag(p)
        }
    }
    Rectangle {
        parent: pluginList
        objectName: "pluginDropIndicator"
        visible: pluginList.dragging && pluginList.targetId.length > 0
        x: 4
        y: pluginList.insertionY
        width: pluginList.width - 8
        height: 2
        radius: 1
        color: pluginList.colors.accent
        z: 3
    }
    Rectangle {
        parent: pluginList
        visible: pluginList.dragging
        x: 8
        y: Math.max(0, Math.min(pluginList.height - height, pluginList.pointerPosition.y + 12))
        width: pluginList.width - 16
        height: 32
        radius: UiMetrics.radiusS
        color: pluginList.colors.panel
        border.color: pluginList.colors.accent
        opacity: 0.92
        z: 4
        Text {
            anchors.fill: parent
            anchors.margins: 6
            text: pluginList.draggedName
            color: pluginList.colors.fg
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: UiMetrics.fontS
        }
    }
    clip: true
    spacing: UiMetrics.space1
    // SettingsPluginSortModel：已启用插件总在前，再按左/中/右
    // 区域与草稿顺序排列。组合分组键令区域标题在
    // 启用状态切换处也保持准确，不会把两段同名区域误合并。
    section.property: "groupKey"
    section.criteria: ViewSection.FullString

    function matchesRow(row) {
        const name = String(model.roleValue(row, "name")
                                  || model.roleValue(row, "id")).toLowerCase()
        const id = String(model.roleValue(row, "id")).toLowerCase()
        return pluginList.filterText.length === 0
            || name.indexOf(pluginList.filterText) >= 0
            || id.indexOf(pluginList.filterText) >= 0
    }
    function sectionHasMatch(groupKey) {
        for (let row = 0; row < count; ++row) {
            if (model.roleValue(row, "groupKey") === groupKey
                    && matchesRow(row)) return true
        }
        return false
    }
    section.delegate: Item {
        width: pluginList.width
        height: pluginList.sectionHasMatch(section) ? 28 : 0
        visible: height > 0
        Text {
            id: sectionName
            anchors.left: parent.left
            anchors.leftMargin: UiMetrics.space2
            anchors.verticalCenter: parent.verticalCenter
            text: pluginList.groupLabel(section)
            color: pluginList.colors.muted
            font.pixelSize: UiMetrics.fontTiny
            font.weight: Font.DemiBold
        }
        Rectangle {
            anchors.left: sectionName.right
            anchors.leftMargin: UiMetrics.space2
            anchors.right: parent.right
            anchors.rightMargin: UiMetrics.space2
            anchors.verticalCenter: parent.verticalCenter
            height: 1
            color: Qt.rgba(pluginList.foregroundColor.r, pluginList.foregroundColor.g,
                           pluginList.foregroundColor.b, 0.14)
        }
    }

    // 选中项按 id 跟踪（勾选启用触发重排后行号会变）
    property string currentId: ""

    function ensureSelection() {
        if (currentId.length > 0 && model.rowForId(currentId) >= 0) return
        currentId = count > 0 ? model.roleValue(0, "id") : ""
    }
    Component.onCompleted: ensureSelection()
    Connections {
        target: pluginList.model
        function onModelReset() { pluginList.cancelDrag(); pluginList.ensureSelection() }
        function onLayoutChanged() { pluginList.cancelDrag() }
        function onRowsRemoved() { pluginList.cancelDrag(); pluginList.ensureSelection() }
        function onRowsMoved() { pluginList.cancelDrag() }
    }

    delegate: Rectangle {
        id: pluginRow
        objectName: "pluginRow_" + model.id
        readonly property string pluginId: model.id
        readonly property string groupKey: model.groupKey
        opacity: pluginList.draggedId === pluginId ? 0.45 : 1
        width: pluginList.width
        property bool matches: pluginList.matchesRow(index)
        height: matches ? 56 : 0
        visible: matches
        radius: UiMetrics.radiusS
        property bool selected: pluginList.currentId === model.id
        readonly property color acc: pluginList.colors.accent
        readonly property color fg: pluginList.colors.fg
        color: selected ? Qt.rgba(fg.r, fg.g, fg.b, 0.07)
             : rowMa.containsMouse ? Qt.rgba(fg.r, fg.g, fg.b, 0.06)
             : "transparent"
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }

        Rectangle {   // 选中 accent 左竖条
            visible: pluginRow.selected
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 26
            radius: 1.5
            color: pluginRow.acc
        }
        Rectangle {   // 启用状态点
            id: dot
            anchors.left: parent.left
            anchors.leftMargin: UiMetrics.space3
            anchors.verticalCenter: parent.verticalCenter
            width: 9; height: 9; radius: 4.5
            color: model.enabled ? pluginList.colors.accent : pluginList.colors.muted
        }
        ColumnLayout {
            anchors.left: dot.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 34
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                color: pluginList.colors.fg
                font.pixelSize: UiMetrics.fontM
                font.weight: pluginRow.selected ? Font.DemiBold : Font.Normal
                text: model.name ? model.name : model.id
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                color: pluginList.colors.muted
                font.pixelSize: UiMetrics.fontTiny
                text: model.id + " · " + (model.kind === "python" ? "Python" : "C++")
                      + (model.discovered ? "" : " · " + pluginList.translateText("磁盘缺失"))
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        MouseArea {
            id: rowMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pluginList.currentId = model.id
        }
        MouseArea {
            id: handle
            objectName: "pluginDragHandle_" + model.id
            anchors.right: parent.right
            width: 32
            height: parent.height
            visible: model.enabled
            enabled: pluginList.filterText.length === 0
            hoverEnabled: true
            preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point pressPoint
            property bool started: false
            onPressed: function(mouse) {
                pressPoint = mapToItem(pluginList, mouse.x, mouse.y)
                started = false
                pluginList.currentId = pluginRow.pluginId
                pluginList.forceActiveFocus()
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const point = mapToItem(pluginList, mouse.x, mouse.y)
                if (!started && Math.abs(point.y - pressPoint.y) >= Qt.styleHints.startDragDistance) {
                    started = true
                    pluginList.beginDrag(pluginRow.pluginId)
                }
                if (started) pluginList.updateDrag(point)
            }
            onReleased: function(mouse) {
                if (started) {
                    pluginList.updateDrag(mapToItem(pluginList, mouse.x, mouse.y))
                    pluginList.finishDrag()
                }
                started = false
            }
            onCanceled: { started = false; pluginList.cancelDrag() }
            ToolTip.visible: containsMouse && !pressed
            ToolTip.text: pluginList.translateText("拖动调整组内顺序")
            // 六点手柄用几何绘制，随 DPI 缩放且不依赖符号字体。
            Grid {
                anchors.centerIn: parent
                columns: 2
                spacing: 3
                opacity: handle.enabled ? 1 : 0.3
                Repeater {
                    model: 6
                    Rectangle { width: 2; height: 2; radius: 1; color: pluginList.colors.muted }
                }
            }
        }
    }
}
