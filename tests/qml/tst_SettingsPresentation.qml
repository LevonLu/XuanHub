// tst_SettingsPresentation.qml — 折叠不丢编辑内容，总开关不清除逐动作草稿
import QtQuick
import QtTest
import XuanHub
import "../../plugins/xuanhub_menu" as Menu
import "../../plugins/sysinfo" as SysInfo

TestCase {
    id: tests
    name: "SettingsPresentation"
    when: windowShown
    visible: true
    width: 640
    height: 720

    QtObject {
        id: draft
        property var values: ({})
        function value(key) { return values[key] }
        function setValue(key, value) {
            const next = Object.assign({}, values)
            next[key] = value
            values = next
        }
    }
    Component {
        id: cardComponent
        UiCard {
            width: 520
            title: "编辑分组"
            collapsible: true
            expanded: true
            UiTextField { objectName: "draftEditor" }
        }
    }
    Component {
        id: menuComponent
        Menu.XuanHubMenuSettings { width: 520 }
    }
    Component {
        id: sysinfoComponent
        SysInfo.SysInfoSettings {}
    }
    function createSysinfoPage(width) {
        draft.values = {itemsPerColumn: 2, items: [
            {metric: "cpu", prefix: "CPU ", color: "#123456"},
            {metric: "mem", prefix: "RAM ", color: ""},
            {metric: "netDown", prefix: "下载 ", color: "#abcdef"}
        ]}
        const page = createTemporaryObject(sysinfoComponent, tests, {width: width})
        verify(page !== null)
        page.plugin = draft
        verify(waitForRendering(page))
        return page
    }
    function sysinfoControlsInside(page) {
        for (let i = 0; i < page.items.length; ++i) {
            const row = findChild(page, "sysinfoMetricRow_" + i)
            if (!row) return false
            const rowPoint = row.mapToItem(page, 0, 0)
            if (rowPoint.x < 0 || rowPoint.x + row.width > page.width + 0.5) return false
            for (const name of ["Metric", "Prefix", "Color", "Swatch", "Up", "Down", "Delete"]) {
                const item = findChild(page, "sysinfo" + name + "_" + i)
                if (!item) return false
                const point = item.mapToItem(row, 0, 0)
                if (point.x < 7.5 || point.y < 7.5 || point.x + item.width > row.width - 7.5
                        || point.y + item.height > row.height - 7.5) return false
            }
        }
        return true
    }
    function test_sysinfo_actions_stay_inside_when_resizing() {
        const page = createSysinfoPage(520)
        for (const width of [520, 360, 440, 620, 360]) {
            page.width = width
            tryVerify(function() { return sysinfoControlsInside(page) }, 1000,
                      "所有输入框与操作按钮必须位于行卡片内，页面宽度=" + width)
        }
    }
    function test_sysinfo_reorder_and_delete_keep_item_fields() {
        const page = createSysinfoPage(520)
        compare(findChild(page, "sysinfoUp_0").enabled, false)
        compare(findChild(page, "sysinfoDown_2").enabled, false)
        let button = findChild(page, "sysinfoDown_0")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(draft.value("items")[1].metric, "cpu")
        compare(draft.value("items")[1].prefix, "CPU ")
        compare(draft.value("items")[1].color, "#123456")
        verify(waitForRendering(page))
        button = findChild(page, "sysinfoUp_1")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(draft.value("items")[0].metric, "cpu")
        verify(waitForRendering(page))
        button = findChild(page, "sysinfoDelete_1")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(draft.value("items").length, 2)
        compare(draft.value("items")[1].metric, "netDown")
        compare(draft.value("items")[1].color, "#abcdef")
    }
    function test_collapsing_preserves_invalid_input_and_keyboard_access() {
        const card = createTemporaryObject(cardComponent, tests)
        verify(card !== null)
        const editor = findChild(card, "draftEditor")
        editor.text = "未完成的输入"
        editor.invalid = true
        verify(waitForRendering(card))
        const expandedHeight = card.implicitHeight
        const header = findChild(card, "cardExpander")
        mouseClick(header, header.width / 2, header.height / 2)
        compare(card.expanded, false)
        tryVerify(function() { return card.implicitHeight < expandedHeight })
        compare(findChild(card, "draftEditor"), editor)
        compare(editor.text, "未完成的输入")
        compare(editor.invalid, true)
        compare(editor.visible, false)
        header.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(card.expanded, true)
        compare(editor.text, "未完成的输入")
        compare(editor.invalid, true)
    }
    function test_menu_master_toggle_keeps_individual_choices() {
        draft.values = {pluginActionsEnabled: true, disabledPluginActions: {sysinfo: ["refresh"], other: ["keep"]}}
        const page = createTemporaryObject(menuComponent, tests)
        verify(page !== null)
        page.plugin = draft
        page.availableActions = [{pluginId: "sysinfo", actionId: "refresh", label: "立即刷新"}]
        verify(waitForRendering(page))
        const master = findChild(page, "menuPluginActionsToggle")
        const action = findChild(page, "menuAction_sysinfo_refresh")
        verify(master !== null && action !== null)
        compare(action.checked, false)
        mouseClick(master, master.width / 2, master.height / 2)
        compare(draft.value("pluginActionsEnabled"), false)
        compare(action.visible, false)
        compare(draft.value("disabledPluginActions").sysinfo[0], "refresh")
        mouseClick(master, master.width / 2, master.height / 2)
        compare(action.checked, false)
        mouseClick(action, action.width / 2, action.height / 2)
        verify(draft.value("disabledPluginActions").sysinfo === undefined)
        compare(draft.value("disabledPluginActions").other[0], "keep")
    }
}
