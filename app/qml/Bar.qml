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
        Item { visible: !barRoot.compact; width: barRoot.compact ? 0 : Math.max(0, (parent.width - leftList.width - centerList.width - rightList.width) / 2); height: 1 }

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

        Item { visible: !barRoot.compact; width: barRoot.compact ? 0 : Math.max(0, (parent.width - leftList.width - centerList.width - rightList.width) / 2); height: 1 }

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
    }
}
