// BarDisplaySettings.qml — 单项任务栏读数选择、格式预览与账号内持久化
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Layouts

Dialog {
    id: settings
    required property var hub
    property string mode:"property"
    property string deviceId:""
    property string propertyId:""
    readonly property var devices:hub.snapshot.catalog || []
    readonly property var queried:hub.snapshot.detail || {}
    readonly property var fields:queried.id===deviceId?(queried.controls || []).filter(f=>f.readable===true):[]
    readonly property var field:fields.filter(f=>f.id===propertyId)[0] || ({})
    readonly property bool canSave:mode==="count" || (!!deviceId && field.id===propertyId)
    readonly property var options:({label:displayName.text,showName:showName.checked,showUnit:showUnit.checked,decimals:decimals.value})
    readonly property string preview:mode==="count"?trText("米家")+" · "+(hub.snapshot.onlineCount || 0)
        :hub.formatBarReading(Object.assign({},field,{deviceName:hub.deviceRecord(deviceId).name}),options)
    font.family:"Microsoft YaHei UI"
    anchors.centerIn:parent
    width:Math.min(420,parent.width-24)
    height:Math.min(560,parent.height-24)
    modal:true
    title:trText("任务栏显示")
    function trText(text) { return qsTranslate("plugin.py_mijia.main",text) }
    function inspect() { hub.inspectedDevice=visible && mode==="property"?deviceId:"" }
    onDeviceIdChanged: { propertyId="";inspect() }
    onModeChanged:inspect()
    onFieldsChanged: {
        if(visible && !propertyId && fields.length)propertyId=fields[0].id
    }
    onOpened: {
        const pin=hub.preferences.pin || {}
        const display=hub.displayOptions
        mode=pin.deviceId?"property":"count"
        deviceId=pin.deviceId || hub.selected || (devices.length?devices[0].id:"")
        propertyId=pin.propertyId || ""
        if(!propertyId && fields.length)propertyId=fields[0].id
        displayName.text=display.label
        showName.checked=display.showName;showUnit.checked=display.showUnit;decimals.value=display.decimals
        inspect()
    }
    onClosed: { hub.inspectedDevice="";hub.scheduleView() }
    contentItem:ScrollView {
        id:scroll
        clip:true;contentWidth:availableWidth
        ColumnLayout {
            width:scroll.availableWidth;spacing:8
            Label { text:settings.trText("显示内容");font.pixelSize:12;color:settings.palette.placeholderText }
            Fluent.ComboBox {
                id:displayMode;objectName:"mijiaDisplayMode"
                Layout.fillWidth:true
                model:[settings.trText("在线设备数"),settings.trText("设备读数")]
                currentIndex:settings.mode==="count"?0:1
                popup.palette:displayMode.palette
                onActivated:function(index){settings.mode=index===0?"count":"property"}
            }
            Label { visible:settings.mode==="property";text:settings.trText("设备");font.pixelSize:12;color:settings.palette.placeholderText }
            Fluent.ComboBox {
                id:devicePicker;objectName:"mijiaDisplayDevice"
                visible:settings.mode==="property";Layout.fillWidth:true
                model:settings.devices.map(d=>({id:d.id,label:d.name+" · "+settings.hub.roomName(d)}));textRole:"label"
                currentIndex:settings.devices.findIndex(d=>d.id===settings.deviceId)
                popup.palette:devicePicker.palette
                onActivated:function(index){settings.deviceId=model[index].id}
            }
            Label { visible:settings.mode==="property";text:settings.trText("可读属性");font.pixelSize:12;color:settings.palette.placeholderText }
            Fluent.ComboBox {
                id:propertyPicker;objectName:"mijiaDisplayProperty"
                visible:settings.mode==="property";Layout.fillWidth:true
                enabled:settings.fields.length>0
                model:settings.fields.map(f=>({id:f.id,label:settings.hub.label(f)+" · "+f.id}));textRole:"label"
                currentIndex:settings.fields.findIndex(f=>f.id===settings.propertyId)
                displayText:currentIndex<0?settings.trText("请选择属性"):currentText
                popup.palette:propertyPicker.palette
                onActivated:function(index){settings.propertyId=model[index].id}
            }
            Label {
                visible:settings.mode==="property" && !settings.fields.length
                Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:12;color:settings.palette.placeholderText
                text:settings.queried.id!==settings.deviceId || settings.queried.support==="loading"?settings.trText("正在解析设备能力…")
                    :settings.queried.support==="no_spec"?settings.trText("该型号尚无可用规格，无法生成控件")
                    :settings.queried.support==="spec_error"?settings.trText("规格获取失败，请稍后刷新")
                    :settings.trText("该设备没有开放可读属性")
            }
            Fluent.CheckBox { id:showName;objectName:"mijiaDisplayShowName";visible:settings.mode==="property";text:settings.trText("显示名称") }
            Fluent.TextField {
                id:displayName;objectName:"mijiaDisplayName"
                visible:settings.mode==="property";enabled:showName.checked;Layout.fillWidth:true
                placeholderText:settings.trText("留空使用设备名称");maximumLength:24
            }
            RowLayout {
                visible:settings.mode==="property";Layout.fillWidth:true
                Fluent.CheckBox { id:showUnit;objectName:"mijiaDisplayShowUnit";text:settings.trText("显示单位");Layout.fillWidth:true }
                Label { text:settings.trText("最大小数位");font.pixelSize:12 }
                Fluent.SpinBox { id:decimals;objectName:"mijiaDisplayDecimals";from:0;to:3 }
            }
            Frame {
                Layout.fillWidth:true
                background:Rectangle { radius:8;color:settings.palette.base;border.color:Qt.rgba(settings.palette.text.r,settings.palette.text.g,settings.palette.text.b,0.08) }
                ColumnLayout {
                    width:parent.width;spacing:8
                    Label { text:settings.trText("预览");font.pixelSize:12;color:settings.palette.placeholderText }
                    Label { objectName:"mijiaDisplayPreview";text:settings.preview;textFormat:Text.PlainText;font.pixelSize:14;wrapMode:Text.Wrap;Layout.fillWidth:true }
                }
            }
            Label { text:settings.trText("关闭面板后，所选读数仍会自动刷新。");visible:settings.mode==="property";font.pixelSize:12;color:settings.palette.placeholderText;wrapMode:Text.Wrap;Layout.fillWidth:true }
        }
    }
    footer:DialogButtonBox {
        Fluent.Button {
            objectName:"mijiaDisplaySave";text:settings.trText("保存");highlighted:true
            enabled:settings.canSave
            DialogButtonBox.buttonRole:DialogButtonBox.ActionRole
            onClicked: {
                if(settings.hub.applyBarDisplay(settings.mode==="count"?"":settings.deviceId,settings.propertyId,settings.options))settings.accept()
            }
        }
        Fluent.Button {
            objectName:"mijiaDisplayCancel";text:settings.trText("取消")
            DialogButtonBox.buttonRole:DialogButtonBox.ActionRole
            onClicked:settings.reject()
        }
    }
}
