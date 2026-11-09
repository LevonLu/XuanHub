// tst_Mijia.qml — 米家偏好隔离、通用写入确认及真实弹窗渲染
import QtQuick
import QtTest
import "../../plugins_py/mijia" as Mijia

TestCase {
    id:tests
    name:"MijiaPlugin"
    when:windowShown
    width:640
    height:720
    visible:true
    QtObject {
        id:vm
        property var values: ({status:"signed_out"})
        property var calls: []
        signal callFinished(string id, bool ok, var result, var error)
        function call(name,args) {
            var next=calls.slice()
            var id=String(next.length+1)
            next.push({id:id,name:name,args:args})
            calls=next
            return {queued:true,id:id}
        }
    }
    QtObject {
        id:context
        property var colors: ({bg:"#202020",panel:"#2b2b2b",fg:"#ffffff",accent:"#60cdff",border:"#484848"})
        property var screenGeometry: ({x:0,y:0,width:1280,height:900})
        property var popupService:null
        property string barEdge:"top"
        property var files: ({})
        signal barPressed()
        function readText(path) { return files[path] ? {success:true,value:files[path]} : {success:false,errorCode:"not_found"} }
        function writeText(path,text) { var next=Object.assign({},files); next[path]=text; files=next; return {success:true} }
    }
    Component { id:component; Mijia.Mijia {} }
    Component { id:propertyComponent; Mijia.PropertyControl { width:400; editable:true } }
    Component { id:cardComponent;Mijia.DeviceCard { width:220 } }
    SignalSpy { id:writeSpy; signalName:"writeRequested" }
    SignalSpy { id:openedSpy;signalName:"opened" }
    function test_multiple_power_channels_open_details() {
        var item=createTemporaryObject(cardComponent,tests,{device:{id:"multi",name:"多路面板",online:true,
            powerCount:2,kind:"switch",preview:[preview("2.1","on",false,"bool","",[],true)]}})
        verify(item!==null)
        writeSpy.target=item;writeSpy.clear()
        openedSpy.target=item;openedSpy.clear()
        var power=findChild(item,"mijiaCardPower")
        mouseClick(power,power.width/2,power.height/2)
        compare(writeSpy.count,0)
        compare(openedSpy.count,1)
    }
    function test_switch_waits_for_device_readback() {
        var field={id:"2.1",name:"on",service:"switch",value:false,readable:true,writable:true,
            pending:false,error:"",editor:"bool",options:[],range:[],primary:true}
        var item=createTemporaryObject(propertyComponent,tests,{field:field,label:"电源"})
        verify(item!==null)
        writeSpy.target=item
        writeSpy.clear()
        var toggle=findChild(item,"mijiaSwitch")
        verify(toggle!==null)
        mouseClick(toggle,toggle.width/2,toggle.height/2)
        compare(writeSpy.count,1)
        compare(writeSpy.signalArguments[0][0],true)
        compare(toggle.checked,false)
        item.field=Object.assign({},field,{value:true})
        compare(toggle.checked,true)
    }
    function state(scope) {
        var field={id:"2.1",name:"temperature",description:"Temperature",service:"environment",value:24.5,
            updatedAt:Date.now()/1000,error:"",pending:false,readable:true,writable:true,primary:false,confirm:true,
            editor:"number",format:"float",range:[0,40,0.5],options:[],unit:"°C"}
        return {status:"ready",error:"",accountScope:scope,onlineCount:1,operations:[],pin:{},
            homes:[{id:"h1",name:"测试家庭",shared:false,rooms:[{id:"h1:r1",name:"书房"}]}],
            catalog:[{id:"d1",name:"书房温湿度计",model:"test.sensor",online:true,homes:["h1"],rooms:["h1:r1"],kind:"sensor",support:"ready"}],
            detail:{id:"d1",name:"书房温湿度计",model:"test.sensor",online:true,kind:"sensor",support:"ready",controls:[field],actions:[]}}
    }
    function init() { vm.values={status:"signed_out"}; vm.calls=[]; context.files=({}) }
    function preview(id,name,value,editor,unit,range,writable) {
        return {id:id,name:name,value:value,editor:editor,unit:unit || "",range:range || [],options:[],
            readable:true,writable:!!writable,confirm:false,pending:false,error:"",stale:false,updatedAt:Date.now()/1000}
    }
    function dashboard() {
        var result=state("dashboard")
        result.detail={}
        result.onlineCount=5
        result.homes[0].name="我的小窝"
        result.homes[0].rooms.push({id:"h1:r2",name:"卧室"})
        function device(id,name,kind,fields,room) {
            return {id:id,name:name,model:"test."+kind,kind:kind,online:true,support:"ready",homes:["h1"],rooms:[room || "h1:r1"],preview:fields}
        }
        result.catalog=[
            device("ac","卧室空调","climate",[preview("2.1","on",false,"bool","",[],true),preview("2.2","target-temperature",26,"number","°C",[16,30,1],true)],"h1:r2"),
            device("lamp","卧室灯","light",[preview("2.1","on",false,"bool","",[],true)],"h1:r2"),
            device("desk","智能显示器挂灯","light",[preview("2.1","on",true,"bool","",[],true)]),
            device("sensor","温湿度计","sensor",[preview("2.1","temperature",26.5,"readout","°C",[-20,60,0.1]),preview("2.2","relative-humidity",70,"readout","%",[0,100,1])]),
            device("plug","电脑插座","switch",[preview("2.1","on",true,"bool","",[],true),preview("3.1","electric-power",18,"readout","W")])]
        return result
    }
    function test_dashboard_cards_filter_and_control() {
        Application.styleHints.colorScheme=Qt.Light
        context.colors={bg:"#f3f3f3",panel:"#ffffff",fg:"#1a1a1a",muted:"#5f5f5f",accent:"#005fb8",border:"#d1d1d1",onAccent:"#ffffff",success:"#0f7b0f"}
        var item=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        vm.values=dashboard()
        tryCompare(item,"accountScope","dashboard")
        var popup=findChild(item,"mijiaPopup")
        popup.closeOnDeactivate=false
        popup.openAnchored()
        wait(150)
        var panel=findChild(item,"mijiaPanel")
        grabImage(panel).save("mijia-overview-light.png")
        var ac=findChild(panel,"mijiaCard_ac")
        var plug=findChild(panel,"mijiaCard_plug")
        verify(ac!==null)
        verify(plug!==null)
        verify(ac.width>plug.width*1.8)
        compare(item.overview.temperature,"26.5°C")
        compare(item.overview.power,"18 W")
        var plus=findChild(ac,"mijiaTemperatureUp")
        mouseClick(plus,plus.width/2,plus.height/2)
        var writes=vm.calls.filter(c=>c.name==="write_property")
        compare(writes.length,1)
        compare(writes[0].args[0].deviceId,"ac")
        compare(writes[0].args[0].propertyId,"2.2")
        compare(writes[0].args[0].value,27)
        compare(ac.target.value,26)
        verify(!plus.enabled)
        var power=findChild(plug,"mijiaCardPower")
        mouseClick(power,power.width/2,power.height/2)
        writes=vm.calls.filter(c=>c.name==="write_property")
        compare(writes.length,2)
        compare(writes[1].args[0].deviceId,"plug")
        compare(writes[1].args[0].value,false)
        compare(plug.on,true)
        item.query="温湿度"
        compare(item.filtered.length,1)
        item.query=""
        item.room="h1:r2"
        compare(item.filtered.length,2)
        item.room=""
        Application.styleHints.colorScheme=Qt.Dark
        context.colors={bg:"#202020",panel:"#2b2b2b",fg:"#ffffff",muted:"#cecece",accent:"#60cdff",border:"#484848",onAccent:"#202020",success:"#6ccb5f"}
        wait(100)
        grabImage(panel).save("mijia-overview-dark.png")
        panel.singleColumn=true
        wait(100)
        verify(findChild(panel,"mijiaCard_ac").width>400)
        popup.width=340
        wait(100)
        verify(findChild(panel,"mijiaCard_plug").width<340)
        grabImage(panel).save("mijia-overview-narrow.png")
        popup.closePopup()
    }
    function test_account_preferences_and_confirmed_write() {
        var item=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        verify(item!==null)
        vm.values=state("accountA")
        tryCompare(item,"accountScope","accountA")
        item.toggleFavorite("d1")
        item.pinField(vm.values.detail.controls[0])
        verify(item.favorite("d1"))
        compare(item.preferences.pin.propertyId,"2.1")
        vm.values=state("accountB")
        tryCompare(item,"accountScope","accountB")
        verify(!item.favorite("d1"))
        compare(item.preferences.pin.deviceId,undefined)
        vm.values=state("accountA")
        tryVerify(function(){return item.favorite("d1")})
        item.requestWrite(vm.values.detail.controls[0],25)
        compare(vm.calls.filter(c=>c.name==="write_property").length,0)
        item.confirmation.args.confirmed=true
        item.call("write_property",[item.confirmation.args],"d1/2.1")
        var call=vm.calls[vm.calls.length-1]
        vm.callFinished(call.id,true,{operationId:"op1"},{})
        compare(item.awaiting["d1/2.1"],"op1")
        var next=state("accountA")
        next.operations=[{id:"op1",deviceId:"d1",propertyId:"2.1",state:"unconfirmed",error:""}]
        vm.values=next
        tryCompare(item,"feedback",item.trText("指令已发送，状态未确认"))
        compare(item.awaiting["d1/2.1"],undefined)
    }
    function test_statusbar_picker_persistence_format_and_cancel() {
        var item=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        var data=state("displayA")
        data.detail.controls[0].value=24.5678
        vm.values=data
        tryCompare(item,"accountScope","displayA")
        var popup=findChild(item,"mijiaPopup")
        popup.closeOnDeactivate=false;popup.openAnchored();wait(100)
        var panel=findChild(item,"mijiaPanel")
        panel.openBarSettings()
        var dialog=findChild(panel,"mijiaDisplaySettings")
        tryCompare(dialog,"visible",true)
        tryCompare(dialog,"deviceId","d1")
        var mode=findChild(dialog,"mijiaDisplayMode")
        mode.activated(1)
        tryCompare(dialog,"propertyId","2.1")
        findChild(dialog,"mijiaDisplayName").text="室温"
        findChild(dialog,"mijiaDisplayDecimals").value=2
        findChild(dialog,"mijiaDisplayShowUnit").checked=false
        compare(dialog.preview,"室温 24.57")
        grabImage(panel).save("mijia-statusbar-settings.png")
        var save=findChild(dialog,"mijiaDisplaySave")
        verify(save.enabled)
        mouseClick(save,save.width/2,save.height/2)
        tryCompare(dialog,"visible",false)
        compare(item.preferences.pin.deviceId,"d1")
        compare(item.preferences.display.label,"室温")
        compare(item.barDisplayText,"室温 —")
        data=state("displayA")
        data.pin=Object.assign({},data.detail.controls[0],{value:24.5678,deviceId:"d1",deviceName:"书房温湿度计"})
        vm.values=data
        tryCompare(item,"barDisplayText","室温 24.57")
        vm.values=Object.assign({},data,{pin:Object.assign({},data.pin,{id:"2.2",value:99})})
        tryCompare(item,"barDisplayText","室温 —")
        vm.values=data
        tryCompare(item,"barDisplayText","室温 24.57")
        compare(item.formatBarReading({value:false,options:[]},{showName:false}),item.trText("已关闭"))
        compare(item.formatBarReading({value:1.25,options:[{value:1.25,label:"枚举读数"}]},{showName:false,decimals:0}),"枚举读数")
        panel.openBarSettings();wait(50)
        tryCompare(dialog,"deviceId","d1")
        findChild(dialog,"mijiaDisplayName").text="不保存"
        var cancel=findChild(dialog,"mijiaDisplayCancel")
        mouseClick(cancel,cancel.width/2,cancel.height/2)
        compare(item.preferences.display.label,"室温")
        vm.values=state("displayB")
        tryCompare(item,"accountScope","displayB")
        compare(item.displayOptions.label,"")
        compare(item.preferences.pin.deviceId,undefined)
        vm.values=data
        tryCompare(item,"accountScope","displayA")
        compare(item.displayOptions.label,"室温")
        var restored=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        tryCompare(restored,"loadedScope","displayA")
        compare(restored.preferences.pin.propertyId,"2.1")
        compare(restored.displayOptions.decimals,2)
        item.clearPin()
        compare(item.barDisplayText,item.trText("米家")+" · 1")
        compare(item.displayOptions.label,"室温")
        popup.closePopup()
    }
    function test_statusbar_picker_queries_readable_fields_without_retargeting_detail() {
        var item=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        var data=state("displayQuery")
        data.catalog.push(Object.assign({},data.catalog[0],{id:"d2",name:"另一台设备"}))
        vm.values=data
        tryCompare(item,"accountScope","displayQuery")
        item.selected="d1"
        var popup=findChild(item,"mijiaPopup")
        popup.closeOnDeactivate=false;popup.openAnchored();wait(100)
        var panel=findChild(item,"mijiaPanel")
        panel.openBarSettings();wait(50)
        var dialog=findChild(panel,"mijiaDisplaySettings")
        tryCompare(dialog,"deviceId","d1")
        findChild(dialog,"mijiaDisplayMode").activated(1)
        findChild(dialog,"mijiaDisplayDevice").activated(1)
        compare(item.inspectedDevice,"d2")
        compare(dialog.canSave,false)
        item.sendView()
        var view=vm.calls.filter(c=>c.name==="set_view").pop().args[0]
        compare(view.selected,"d2")
        compare(view.ids.length,0)
        data.detail=Object.assign({},data.detail,{id:"d2",controls:[
            Object.assign({},data.detail.controls[0],{id:"3.1"}),
            Object.assign({},data.detail.controls[0],{id:"3.2",readable:false})]})
        vm.values=data
        tryCompare(dialog,"propertyId","3.1")
        compare(dialog.fields.length,1)
        compare(item.selected,"d1")
        compare(item.detail.id,"d1")
        compare(item.detail.controls.length,0)
        dialog.reject()
        tryCompare(item,"inspectedDevice","")
        item.sendView()
        view=vm.calls.filter(c=>c.name==="set_view").pop().args[0]
        compare(view.selected,"d1")
        popup.closePopup()
    }
    function test_popup_login_and_details_render() {
        var item=createTemporaryObject(component,tests,{plugin:vm,pluginContext:context})
        verify(item!==null)
        var popup=findChild(item,"mijiaPopup")
        verify(popup!==null)
        popup.closeOnDeactivate=false
        popup.openAnchored()
        tryCompare(popup,"visible",true)
        wait(100)
        var panel=findChild(item,"mijiaPanel")
        verify(panel!==null)
        verify(grabImage(panel).width>0)
        grabImage(panel).save("mijia-login.png")
        vm.values=state("accountA")
        item.selected="d1"
        wait(150)
        verify(grabImage(panel).width>0)
        grabImage(panel).save("mijia-detail.png")
        Application.styleHints.colorScheme=Qt.Light
        context.colors={bg:"#ffffff",panel:"#f5f5f5",fg:"#111111",accent:"#0067c0",border:"#cccccc"}
        wait(100)
        grabImage(panel).save("mijia-detail-light.png")
        popup.closePopup()
        compare(popup.visible,false)
    }
}
