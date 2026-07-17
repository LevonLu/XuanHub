// Bar.qml — 状态栏主体。M3：三栏布局（left/center/right）。
// RowLayout + 三个 ListView（pluginModel.leftPlugins / centerPlugins / rightPlugins）。
import QtQuick
import QtQuick.Layouts

Item {
    id: barRoot

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── LEFT ──
        ListView {
            id: leftList
            model: pluginModel.leftPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            Layout.fillHeight: true
            Layout.preferredWidth: contentWidth
        }

        Item { Layout.fillWidth: true }   // 左弹簧

        // ── CENTER ──
        ListView {
            id: centerList
            model: pluginModel.centerPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            Layout.fillHeight: true
            Layout.preferredWidth: contentWidth
        }

        Item { Layout.fillWidth: true }   // 右弹簧

        // ── RIGHT ──
        ListView {
            id: rightList
            model: pluginModel.rightPlugins
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            Layout.fillHeight: true
            Layout.preferredWidth: contentWidth
        }
    }
}
