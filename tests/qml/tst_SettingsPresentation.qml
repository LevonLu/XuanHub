// tst_SettingsPresentation.qml — 折叠不丢编辑内容，总开关不清除逐动作草稿
import QtQuick
import QtTest
import XuanHub
import "../../plugins/xuanhub_menu" as Menu

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
