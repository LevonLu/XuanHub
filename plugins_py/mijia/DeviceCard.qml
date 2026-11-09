// DeviceCard.qml — 空调横卡、传感器读数卡与可直接操作的设备卡
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: card
    // 卡片可独立嵌入详情，不依赖父控件向普通 Item 传递字体。
    property string fontFamily:"Microsoft YaHei UI"
    property var device: ({})
    property string roomName: ""
    property color surface: "#ffffff"
    property color foreground: "#24272b"
    property color muted: "#8a8e95"
    property color accent: "#1684ff"
    property bool dark: false
    property bool busy: false
    readonly property bool climate:device.kind === "climate"
    readonly property bool sensor:device.kind === "sensor"
    readonly property var powerFields:(device.preview || []).filter(c=>c.name === "on" && c.editor === "bool")
    readonly property var power:powerFields.length === 1 && (device.powerCount || 1) === 1 ? powerFields[0] : ({})
    readonly property var target:find("target-temperature")
    readonly property var temperature:find("temperature")
    readonly property var humidity:find("relative-humidity")
    readonly property var watts:find("electric-power")
    readonly property var primaryReading:temperature.value !== undefined ? temperature : (device.preview || []).filter(c=>c.name !== "battery-level" && c.name !== "on")[0] || ({})
    readonly property bool on:power.value === true
    readonly property bool cached:climate?target.stale===true:power.stale===true
    implicitHeight:climate?88:80
    signal opened()
    signal writeRequested(var field,var value)
    function trText(source) { return qsTranslate("plugin.py_mijia.main",source) }
    function find(name) { return (device.preview || []).filter(c=>c.name===name)[0] || ({}) }
    function number(field) { return typeof field.value === "number" ? Number(field.value.toFixed(1)).toString() : "—" }
    function reading(field) {
        if(typeof field.value === "boolean") return field.value?trText("打开"):trText("关闭")
        const choice=(field.options || []).filter(o=>o.value === field.value)[0]
        return choice?choice.label:number(field)
    }
    function canWrite(field) { return !!(device.online && field.writable && !field.pending && !busy) }
    function adjust(delta) {
        if (!canWrite(target) || !target.range || target.range.length!==3 || typeof target.value!=="number") return
        const limits=target.range
        const next=Math.max(limits[0],Math.min(limits[1],target.value+delta*limits[2]))
        card.writeRequested(target,Number(next.toFixed(6)))
    }
    function subtitle() {
        var state = !device.online ? trText("离线") : power.value === true ? (device.kind==="switch"?trText("供电中"):trText("已开启"))
            : power.value === false ? trText("已关闭") : (device.powerCount || powerFields.length)>1 ? trText("多路开关")
            : device.support === "loading" ? trText("加载中…") : device.support === "no_spec" ? trText("暂无可用规格") : trText("在线")
        if (busy) state=trText("执行中…")
        else if (cached && device.online) state += " · " + trText("缓存")
        if (on && typeof watts.value === "number") state += " · " + number(watts) + " " + (watts.unit || "W")
        return (roomName ? roomName + " · " : "") + state
    }
    Rectangle {
        anchors.fill:parent;radius:8
        color:cardArea.containsMouse?Qt.tint(card.surface,Qt.rgba(card.foreground.r,card.foreground.g,card.foreground.b,0.025)):card.surface
        border.color:Qt.rgba(card.foreground.r,card.foreground.g,card.foreground.b,0.08)
        border.width:1
    }
    MouseArea { id:cardArea;anchors.fill:parent;hoverEnabled:true;cursorShape:Qt.PointingHandCursor;onClicked:card.opened() }
    Item {
        anchors.fill:parent;anchors.margins:12
        opacity:card.device.online?1:0.55
        RowLayout {
            visible:!card.sensor
            anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top
            height:card.climate?46:parent.height
            spacing:10
            MijiaIconButton {
                objectName:"mijiaCardPower"
                implicitWidth:32;implicitHeight:32;padding:8
                glyph:card.device.kind || "generic"
                tint:card.foreground
                flat:!card.power.writable
                highlighted:card.on
                hint:card.power.writable?card.trText("切换电源"):card.trText("设备详情")
                onClicked: {
                    if(card.canWrite(card.power))card.writeRequested(card.power,!card.on)
                    else card.opened()
                }
            }
            ColumnLayout {
                Layout.fillWidth:true;spacing:4
                Label { font.family:card.fontFamily; text:card.device.name || "";textFormat:Text.PlainText;color:card.foreground;font.pixelSize:14;font.weight:Font.DemiBold;Layout.fillWidth:true;wrapMode:Text.Wrap;maximumLineCount:2;elide:Text.ElideRight }
                Label { font.family:card.fontFamily; text:card.subtitle();color:card.muted;font.pixelSize:12;Layout.fillWidth:true;elide:Text.ElideRight }
            }
            RowLayout {
                visible:card.climate;spacing:8
                MijiaIconButton { objectName:"mijiaTemperatureDown";glyph:"minus";flat:false;tint:card.foreground;enabled:card.canWrite(card.target);onClicked:card.adjust(-1);hint:card.trText("降低温度") }
                Label { font.family:card.fontFamily;text:card.number(card.target)+(card.target.unit || "°C");color:card.foreground;font.pixelSize:20;Layout.preferredWidth:58;horizontalAlignment:Text.AlignHCenter }
                MijiaIconButton { objectName:"mijiaTemperatureUp";glyph:"plus";flat:false;tint:card.foreground;enabled:card.canWrite(card.target);onClicked:card.adjust(1);hint:card.trText("升高温度") }
            }
            DeviceGlyph { visible:!card.climate;kind:"next";tint:card.muted;Layout.preferredWidth:13;Layout.preferredHeight:13 }
        }
        ColumnLayout {
            visible:card.sensor;anchors.fill:parent;spacing:4
            RowLayout {
                Layout.fillWidth:true
                Label { font.family:card.fontFamily; text:card.device.name || "";color:card.foreground;font.pixelSize:12;Layout.fillWidth:true;elide:Text.ElideRight;textFormat:Text.PlainText }
                Label {
                    visible:!card.device.online || (card.primaryReading.value!==undefined && card.primaryReading.value!==null)
                    font.family:card.fontFamily
                    text:!card.device.online?card.trText("离线"):card.primaryReading.stale?card.trText("缓存"):card.trText("实时")
                    font.pixelSize:11;color:card.primaryReading.stale || !card.device.online?card.muted:card.accent
                }
            }
            RowLayout {
                Layout.fillWidth:true;spacing:5
                Label { font.family:card.fontFamily; text:card.reading(card.primaryReading);font.pixelSize:24;color:card.foreground }
                Label { font.family:card.fontFamily; text:card.primaryReading.unit || "";font.pixelSize:11;color:card.muted;Layout.alignment:Qt.AlignBottom;Layout.bottomMargin:4 }
                Item { Layout.fillWidth:true }
                DeviceGlyph { visible:typeof card.humidity.value==="number";kind:"humidity";tint:card.accent;Layout.preferredWidth:12;Layout.preferredHeight:16 }
                Label { font.family:card.fontFamily; visible:typeof card.humidity.value==="number";text:card.number(card.humidity)+"%";color:card.accent;font.pixelSize:12 }
            }
        }
        Rectangle {
            visible:card.climate || !!(card.sensor && card.primaryReading.range && card.primaryReading.range.length===3)
            anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom
            height:2;radius:1;color:Qt.rgba(card.muted.r,card.muted.g,card.muted.b,0.12)
            Rectangle {
                height:parent.height;radius:2;color:card.accent
                width: {
                    const field=card.climate?card.target:card.primaryReading
                    if(typeof field.value!=="number" || !field.range || field.range.length!==3) return 0
                    return parent.width*Math.max(0,Math.min(1,(field.value-field.range[0])/Math.max(1,field.range[1]-field.range[0])))
                }
            }
        }
    }
}
