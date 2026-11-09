// Mijia.qml — 米家状态栏、扫码和设备详情，偏好按账号摘要隔离
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    readonly property var snapshot: plugin ? plugin.values : ({})
    readonly property var colors: pluginContext ? pluginContext.colors : ({})
    readonly property color panelColor:colors.bg || "#f3f3f3"
    readonly property bool dark:panelColor.r*0.299+panelColor.g*0.587+panelColor.b*0.114<0.5
    readonly property color cardColor:colors.panel || (dark?"#2b2b2b":"#ffffff")
    readonly property color foreground:colors.fg || (dark?"#f1f3f5":"#24272b")
    readonly property color muted:colors.muted || (dark?"#cecece":"#5f5f5f")
    readonly property color accent:colors.accent || (dark?"#60cdff":"#005fb8")
    readonly property var overview:environmentSummary()
    readonly property string status: snapshot.status || "signed_out"
    readonly property string accountScope: snapshot.accountScope || ""
    readonly property bool ready: status === "ready"
    readonly property var detail: selectedDetail()
    property var preferences: ({favorites: [], home: "", pin: {}})
    property string loadedScope: ""
    property string selected: ""
    property string inspectedDevice: ""
    property string room: ""
    property string query: ""
    property bool favoritesOnly: false
    property string feedback: ""
    property string lastView: ""
    property var confirmation: ({})
    property var requests: ({})
    property var awaiting: ({})
    readonly property var filtered: filterDevices()
    readonly property var displayOptions:normalizeDisplay(preferences.display || {})
    readonly property string barDisplayText:statusBarText()
    width: Math.min(280, barText.implicitWidth + 44)
    height: 40

    function trText(source) { return qsTranslate("plugin.py_mijia.main", source) }
    function controlKey(deviceId, propertyId) { return deviceId + "/" + propertyId }
    function alpha(color,opacity) { const c=Qt.darker(color,1);return Qt.rgba(c.r,c.g,c.b,opacity) }
    function roomName(device) {
        var names=[]
        ;(snapshot.homes || []).forEach(function(home) {
            ;(home.rooms || []).forEach(function(room) { if((device.rooms || []).indexOf(room.id)>=0)names.push(room.name) })
        })
        return names[0] || trText("未分配房间")
    }
    function environmentSummary() {
        var summary={temperature:"",humidity:"",power:""}, watts=0, metered=false
        filtered.filter(d=>d.online).forEach(function(device) {
            ;(device.preview || []).forEach(function(field) {
                if(field.stale || field.error || typeof field.value!=="number")return
                if(field.name==="temperature" && !summary.temperature)summary.temperature=Number(field.value.toFixed(1))+(field.unit || "°C")
                if(field.name==="relative-humidity" && !summary.humidity)summary.humidity=Number(field.value.toFixed(1))+"%"
                if(field.name==="electric-power" && field.unit==="W"){watts+=field.value;metered=true}
            })
        })
        if(metered)summary.power=Number(watts.toFixed(1))+" W"
        return summary
    }
    function label(field) {
        const names = {"on":"电源", "brightness":"亮度", "color-temperature":"色温", "mode":"模式",
            "target-temperature":"目标温度", "temperature":"温度", "relative-humidity":"湿度",
            "target-humidity":"目标湿度", "fan-level":"风速", "speed-level":"档位",
            "horizontal-swing":"左右摆风", "vertical-swing":"上下摆风", "motor-control":"窗帘控制",
            "target-position":"目标位置", "current-position":"当前位置", "pm2.5-density":"PM2.5",
            "filter-life-level":"滤芯余量", "battery-level":"电量", "electric-power":"功率",
            "illumination":"照度", "contact-state":"门窗状态", "door-state":"门锁状态",
            "motion-state":"移动状态", "occupancy-status":"有人状态", "air-quality":"空气质量",
            "water-leakage-state":"漏水状态", "open":"打开", "close":"关闭", "stop":"停止"}
        return names[field.name] ? trText(names[field.name]) : (field.description || field.name || "")
    }
    function kindLabel(kind) {
        return trText(({light:"灯", switch:"插座与开关", climate:"空调", fan:"风扇", cover:"窗帘",
                        air:"空气设备", sensor:"传感器", generic:"通用设备"})[kind] || "通用设备")
    }
    function errorText(code) {
        const messages = {network_error:"网络连接失败，稍后重试", rate_limited:"请求过于频繁，已放慢刷新",
            session_expired:"登录已过期，请重新扫码", qr_expired:"二维码已过期，请刷新",
            invalid_response:"服务响应异常", storage_error:"本机凭据或偏好保存失败",
            host_upgrade_required:"请升级宿主以启用米家密钥服务", invalid_session:"已保存的会话无效，请重新扫码",
            invalid_argument:"参数不符合设备规格", read_only:"该属性只读", device_error:"设备未响应或拒绝了操作",
            device_offline:"设备离线", busy:"操作进行中，请稍后再试", not_signed_in:"请先扫码登录",
            permission_denied:"宿主未授予所需权限", confirmation_required:"请先确认此操作", api_error:"米家云拒绝了请求"}
        return code ? trText(messages[code] || "操作失败") : ""
    }
    function valueText(field) {
        if (field.value === undefined || field.value === null) return "—"
        if (typeof field.value === "boolean") return field.value ? trText("已开启") : trText("已关闭")
        const options = field.options || []
        for (var i = 0; i < options.length; ++i)
            if (options[i].value === field.value) return options[i].label
        return String(field.value) + (field.unit ? " " + field.unit : "")
    }
    function normalizeDisplay(raw) {
        raw=raw || {}
        return {label:typeof raw.label==="string"?raw.label.slice(0,24):"",
            showName:raw.showName!==false,showUnit:raw.showUnit!==false,
            decimals:Number.isInteger(raw.decimals) && raw.decimals>=0 && raw.decimals<=3?raw.decimals:1}
    }
    function deviceRecord(id) { return (snapshot.catalog || []).filter(d=>d.id===id)[0] || ({}) }
    function selectedDetail() {
        const raw=snapshot.detail || {}
        // 选择器临时借用详情查询时，不让原设备页面显示另一台设备的控件。
        if(!selected || raw.id===selected)return raw
        return Object.assign({},deviceRecord(selected),{controls:[],actions:[],support:"loading"})
    }
    function formatBarReading(field,options) {
        const setting=normalizeDisplay(options || {})
        const value=typeof field.value==="number" && !(field.options || []).length?Number(field.value.toFixed(setting.decimals)):field.value
        const reading=valueText(Object.assign({},field,{value:value,unit:setting.showUnit?field.unit:""}))
        const name=setting.showName?(setting.label.trim() || field.deviceName || ""):""
        return name?name+" "+reading:reading
    }
    function statusBarText() {
        if(!ready)return trText("米家")+" · "+trText("未连接")
        const choice=preferences.pin || {}
        if(!choice.deviceId)return trText("米家")+" · "+(snapshot.onlineCount || 0)
        const pin=snapshot.pin || {}
        if(pin.deviceId===choice.deviceId && pin.missing)return trText("请重新选择读数")
        // 新选择立即生效，不把上一项属性的迟到快照显示成新选择的值。
        const matches=pin.deviceId===choice.deviceId && pin.id===choice.propertyId
        return formatBarReading(matches?pin:{deviceName:deviceRecord(choice.deviceId).name,value:null},displayOptions)
    }
    function call(name, args, key) {
        if (!plugin) return
        const receipt = plugin.call(name, args || [])
        if (!receipt.queued) { feedback = trText("操作失败"); return }
        var next = Object.assign({}, requests)
        next[receipt.id] = {key:key || "", name:name}
        requests = next
        if (key) {
            var wait = Object.assign({}, awaiting)
            wait[key] = "queued"
            awaiting = wait
        }
    }
    function loadPreferences() {
        if (!pluginContext || !accountScope || loadedScope === accountScope) return
        loadedScope = accountScope
        var result = pluginContext.readText("preferences-" + accountScope + ".json")
        var pref = {favorites: [], home: "", pin: {}}
        if (result.success) {
            try {
                var raw = JSON.parse(result.value)
                if (Array.isArray(raw.favorites)) pref.favorites = raw.favorites.filter(v => typeof v === "string")
                if (typeof raw.home === "string") pref.home = raw.home
                if (raw.pin && typeof raw.pin.deviceId === "string" && typeof raw.pin.propertyId === "string") pref.pin = raw.pin
                pref.display=normalizeDisplay(raw.display || {})
            } catch (e) { feedback = trText("本机凭据或偏好保存失败") }
        } else if (result.errorCode !== "not_found") feedback = trText("本机凭据或偏好保存失败")
        preferences = pref
        scheduleView()
    }
    function savePreferences(pref) {
        preferences = pref
        if (pluginContext && accountScope && loadedScope === accountScope) {
            var result = pluginContext.writeText("preferences-" + accountScope + ".json", JSON.stringify(pref))
            if (!result.success) feedback = trText("本机凭据或偏好保存失败")
        }
        scheduleView()
    }
    function favorite(id) { return (preferences.favorites || []).indexOf(id) >= 0 }
    function toggleFavorite(id) {
        var pref = JSON.parse(JSON.stringify(preferences))
        pref.favorites = favorite(id) ? pref.favorites.filter(v => v !== id) : pref.favorites.concat([id])
        savePreferences(pref)
    }
    function pinField(field) {
        if(!detail.id || field.readable!==true)return
        var pref = JSON.parse(JSON.stringify(preferences))
        pref.pin = {deviceId: detail.id, propertyId: field.id}
        savePreferences(pref)
        feedback=trText("已更新任务栏显示")
    }
    function clearPin() {
        var pref=JSON.parse(JSON.stringify(preferences));pref.pin={};savePreferences(pref)
    }
    function applyBarDisplay(deviceId,propertyId,options) {
        const raw=snapshot.detail || {}
        if(deviceId && (raw.id!==deviceId || !(raw.controls || []).some(c=>c.id===propertyId && c.readable===true)))return false
        var pref=JSON.parse(JSON.stringify(preferences))
        pref.pin=deviceId?{deviceId:deviceId,propertyId:propertyId}:{}
        pref.display=normalizeDisplay(options)
        savePreferences(pref)
        feedback=trText("已更新任务栏显示")
        return true
    }
    function filterDevices() {
        var query = root.query.trim().toLowerCase()
        var catalog=snapshot.catalog || []
        return catalog.filter(function(device) {
            return (!preferences.home || device.homes.indexOf(preferences.home) >= 0)
                && (!room || device.rooms.indexOf(room) >= 0)
                && (!favoritesOnly || favorite(device.id))
                && (!query || (device.name + " " + device.model).toLowerCase().indexOf(query) >= 0)
        }).sort((a,b)=>(a.kind==="climate"?0:1)-(b.kind==="climate"?0:1) || catalog.indexOf(a)-catalog.indexOf(b))
    }
    function homeOptions() {
        return [{id:"", name:trText("全部家庭")}].concat((snapshot.homes || []).map(h => ({
            id:h.id, name:h.name + (h.shared ? " · " + trText("共享") : "")})))
    }
    function roomOptions() {
        var rooms = []
        ;(snapshot.homes || []).forEach(function(home) {
            if (!preferences.home || home.id === preferences.home) rooms = rooms.concat(home.rooms)
        })
        return [{id:"", name:trText("全部房间")}].concat(rooms)
    }
    function scheduleView() { viewTimer.restart() }
    function sendView() {
        if (!ready) return
        var ids = panel.visibleDeviceIds()
        var view = {open:popup.visible, ids:ids, selected:inspectedDevice || selected, pin:preferences.pin || {}}
        var signature = JSON.stringify(view)
        if (signature !== lastView) { lastView = signature; call("set_view", [view]) }
    }
    function requestWrite(field, value) {
        requestDeviceWrite(detail,field,value)
    }
    function requestDeviceWrite(device,field,value) {
        var args = {deviceId:device.id, propertyId:field.id, value:value, confirmed:!field.confirm}
        if (field.confirm) {
            confirmation = {args:args, text:device.name + "\n" + label(field) + " → " + valueText(Object.assign({}, field, {value:value}))}
            panel.openConfirmation()
        } else call("write_property", [args], controlKey(args.deviceId, field.id))
    }
    function synchronize() {
        loadPreferences()
        if (snapshot.catalogLoaded) {
            if (preferences.home && !(snapshot.homes || []).some(h => h.id === preferences.home)) {
                var pref = JSON.parse(JSON.stringify(preferences))
                pref.home = ""; room = ""; savePreferences(pref)
            }
            if (selected && !(snapshot.catalog || []).some(d => d.id === selected)) {
                selected = ""; feedback = trText("设备已移除，请重新选择")
            }
        }
        var pending = Object.assign({}, awaiting)
        ;(snapshot.operations || []).forEach(function(op) {
            if (op.state === "pending") return
            var key = controlKey(op.deviceId, op.propertyId)
            if (pending[key] === op.id) {
                delete pending[key]
                feedback = op.state === "success" ? trText("设备状态已确认")
                         : op.state === "unconfirmed" ? trText("指令已发送，状态未确认") : errorText(op.error)
            }
        })
        awaiting = pending
        scheduleView()
    }
    onPluginContextChanged: loadPreferences()
    onAccountScopeChanged: {
        selected = ""; inspectedDevice="";room = ""; lastView = ""; loadedScope = ""; awaiting = ({}); requests = ({})
        preferences = {favorites:[], home:"", pin:{}}
        if (accountScope) loadPreferences()
        if(panel)panel.closeDialogs()
    }
    onFilteredChanged: scheduleView()
    onSelectedChanged: scheduleView()
    onInspectedDeviceChanged:scheduleView()
    Connections {
        target: root.plugin || null
        // 等属性绑定稳定后再处理回执，避免信号处理器读到前一份快照。
        function onValuesChanged() { Qt.callLater(root.synchronize) }
        function onCallFinished(id, ok, result, error) {
            var request = root.requests[id] || ({})
            var key = request.key
            var requests = Object.assign({}, root.requests)
            delete requests[id]
            root.requests = requests
            if (!ok) {
                if (request.name === "set_view") root.lastView = ""
                root.feedback = root.errorText(error.code)
                var pending = Object.assign({}, root.awaiting)
                if (key) delete pending[key]
                root.awaiting = pending
            } else if (key && result && result.operationId) {
                var next = Object.assign({}, root.awaiting)
                next[key] = result.operationId
                root.awaiting = next
            }
        }
    }
    Timer { id:viewTimer; interval:100; onTriggered:root.sendView() }
    Rectangle { anchors.fill:parent;radius:4;color:barMouse.containsMouse?root.alpha(root.foreground,0.06):"transparent" }
    Row {
        anchors.centerIn:parent;spacing:7
        DeviceGlyph { width:16;height:16;anchors.verticalCenter:parent.verticalCenter;kind:"home";tint:root.snapshot.pin && root.snapshot.pin.stale?root.muted:root.accent }
        Text {
            id:barText
            objectName:"mijiaBarText"
            anchors.verticalCenter:parent.verticalCenter
            font.family:"Microsoft YaHei UI";font.pixelSize:13;color:root.foreground;width:Math.min(228,implicitWidth);elide:Text.ElideRight
            text:root.barDisplayText
        }
    }
    MouseArea { id:barMouse;anchors.fill:parent;hoverEnabled:true;cursorShape:Qt.PointingHandCursor;onClicked:popup.toggleFromBar() }
    PluginPopup {
        id:popup
        objectName:"mijiaPopup"
        anchorItem:root
        pluginContext:root.pluginContext
        width:Math.min(480,root.pluginContext && root.pluginContext.screenGeometry?root.pluginContext.screenGeometry.width-16:480)
        height:Math.min(root.ready?620:560,root.pluginContext && root.pluginContext.screenGeometry?root.pluginContext.screenGeometry.height-24:650)
        onOpened:{root.feedback="";root.scheduleView()}
        onClosed:{root.scheduleView();panel.closeDialogs()}
        MijiaPanel { id:panel;objectName:"mijiaPanel";anchors.fill:parent;hub:root }
    }
}
