// Bar.qml — 状态栏主体。三栏布局（left/center/right）。
// taskbar 模式（barConfig.barMode === "taskbar"）：弹性间隔归 0，三段紧凑排列，
// 此时 implicitWidth 即内容实际宽度，BarWindow 据此收缩窗口贴合任务栏。
import QtQuick
import XuanHub

Item {
    id: barRoot

    readonly property bool compact: barConfig && barConfig.barMode === "taskbar"
    implicitWidth: row.implicitWidth

    function updateLayoutBudget() {
        if (!pluginModel) return
        pluginModel.setAvailableWidth(compact ? 0
            : Math.max(0, width - (overflowButton.visible ? overflowButton.width : 0)))
    }
    Component.onCompleted: updateLayoutBudget()
    onWidthChanged: updateLayoutBudget()
    onCompactChanged: updateLayoutBudget()

    Connections {
        target: pluginModel
        function onResponsiveLayoutChanged() {
            Qt.callLater(barRoot.updateLayoutBudget)
        }
    }

    Row {
        id: row
        anchors.fill: parent
        // taskbar 紧凑模式下弹性间隔归 0，各部件会紧挨在一起，留 6px 呼吸位；
        // 空 ListView 经 visible=count>0 隐藏，positioner 不为其产生间距
        spacing: barRoot.compact ? 6 : 0

        ListView {
            id: leftList
            model: pluginModel.leftPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            visible: count > 0
            height: parent.height
            width: contentWidth
        }

        // 弹性间隔：紧凑模式宽 0 且隐藏（否则 Row.spacing 会在其两侧产生死间隙）。
        // Math.max(0, ...)：三栏内容总宽超过 bar 宽（小屏/插件过多/固定宽过大）时
        // 不 clamp 会得到负宽度，把后续栏向左拉导致插件互相重叠绘制
        Item { visible: !barRoot.compact; width: barRoot.compact ? 0 : Math.max(0, (parent.width - leftList.width - centerList.width - rightList.width - overflowButton.width) / 2); height: 1 }

        ListView {
            id: centerList
            model: pluginModel.centerPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            visible: count > 0
            height: parent.height
            width: contentWidth
        }

        Item { visible: !barRoot.compact; width: barRoot.compact ? 0 : Math.max(0, (parent.width - leftList.width - centerList.width - rightList.width - overflowButton.width) / 2); height: 1 }

        ListView {
            id: rightList
            model: pluginModel.rightPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            visible: count > 0
            height: parent.height
            width: contentWidth
        }

        Item {
            id: overflowButton
            visible: !barRoot.compact && pluginModel.overflowPlugins.length > 0
            width: visible ? 34 : 0
            height: parent.height
            onVisibleChanged: Qt.callLater(barRoot.updateLayoutBudget)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 6
                color: overflowArea.containsMouse
                       ? themeManager.colors.hover : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "⋯"
                color: themeManager.colors.fg
                font.pixelSize: 20
                font.bold: true
            }
            MouseArea {
                id: overflowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: overflowPopup.toggleFromBar()
            }
        }
    }

    PluginPopup {
        id: overflowPopup
        anchorItem: overflowButton
        width: Math.min(520, Math.max(260, overflowColumn.implicitWidth + 24))
        height: Math.min(420, overflowColumn.implicitHeight + 24)

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: themeManager.colors.bg
            border.width: 1
            border.color: themeManager.colors.border

            Flickable {
                anchors.fill: parent
                anchors.margins: 12
                contentWidth: overflowColumn.implicitWidth
                contentHeight: overflowColumn.implicitHeight
                clip: true

                Column {
                    id: overflowColumn
                    spacing: 6
                    Text {
                        text: qsTranslate("xuanhub.bar", "更多插件")
                        color: themeManager.colors.fg
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Repeater {
                        model: pluginModel.overflowPluginModel
                        delegate: PluginSlot { overflowHost: true }
                    }
                }
            }
        }
    }
}
