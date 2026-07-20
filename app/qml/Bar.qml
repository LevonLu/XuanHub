// Bar.qml — 状态栏主体。三栏布局（left/center/right）。
import QtQuick

Item {
    id: barRoot

    Row {
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

        Item { width: (parent.width - leftList.width - centerList.width - rightList.width) / 2; height: 1 }

        ListView {
            id: centerList
            model: pluginModel.centerPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            height: parent.height
            width: contentWidth
        }

        Item { width: (parent.width - leftList.width - centerList.width - rightList.width) / 2; height: 1 }

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
