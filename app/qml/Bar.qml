// Bar.qml — 状态栏主体。三栏布局（left/center/right）。
// taskbar 模式（barConfig.barMode === "taskbar"）：弹性间隔归 0，三段紧凑排列，
// 此时 implicitWidth 即内容实际宽度，BarWindow 据此收缩窗口贴合任务栏。
import QtQuick

Item {
    id: barRoot

    readonly property bool compact: barConfig && barConfig.barMode === "taskbar"
    implicitWidth: row.implicitWidth

    Row {
        id: row
        anchors.fill: parent

        ListView {
            id: leftList
            model: pluginModel.leftPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            height: parent.height
            width: contentWidth
        }

        Item { width: barRoot.compact ? 0 : (parent.width - leftList.width - centerList.width - rightList.width) / 2; height: 1 }

        ListView {
            id: centerList
            model: pluginModel.centerPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            height: parent.height
            width: contentWidth
        }

        Item { width: barRoot.compact ? 0 : (parent.width - leftList.width - centerList.width - rightList.width) / 2; height: 1 }

        ListView {
            id: rightList
            model: pluginModel.rightPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            height: parent.height
            width: contentWidth
        }
    }
}
