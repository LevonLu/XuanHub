// PixelLandscapeSettings.qml — 城市预设只填充设置草稿，保存仍由宿主负责
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub
import "CityPresets.js" as Cities

Item {
    id: root
    property var plugin
    property int countryIndex: 0
    property bool customCoordinates: false
    implicitHeight: body.implicitHeight
    function tr(source) { return qsTranslate("plugin.pixel_landscape.settings", source) }
    function read(key, fallback) {
        if (!plugin || plugin.pluginId !== "pixel_landscape") return fallback
        const revision = plugin.revision
        const value = plugin.value(key)
        return value === undefined || value === null ? fallback : value
    }
    function put(key, value) {
        if (plugin && plugin.pluginId === "pixel_landscape") plugin.setValue(key, value)
    }
    function validCoordinate(text, limit) {
        return String(text).trim().length > 0 && isFinite(Number(text)) && Math.abs(Number(text)) <= limit
    }
    function enableManual(enabled) {
        if (enabled) {
            // 只有显式启用才补齐坐标，打开设置页不会把缺省 0,0 写成已配置位置。
            if (read("latitude", null) === null) put("latitude", 0)
            if (read("longitude", null) === null) put("longitude", 0)
        }
        put("manualLocationEnabled", enabled)
    }
    readonly property var countries: Cities.countries
    readonly property var cities: countries[countryIndex].cities
    readonly property int matchedCity: {
        if (customCoordinates) return -1
        const lat = read("latitude", null), lon = read("longitude", null)
        return Cities.findCity(cities, lat, lon)
    }
    function restoreCountry() {
        customCoordinates = false
        countryIndex = 0
        const lat = read("latitude", null), lon = read("longitude", null)
        for (let i = 0; i < countries.length; ++i) {
            if (Cities.findCity(countries[i].cities, lat, lon) >= 0) { countryIndex = i; break }
        }
    }
    function chooseCity(index) {
        customCoordinates = index === 0
        if (index <= 0 || index > cities.length) return
        const city = cities[index - 1]
        put("latitude", city.latitude)
        put("longitude", city.longitude)
        put("manualLocationEnabled", true)
    }
    onPluginChanged: restoreCountry()
    Connections {
        target: root.plugin || null
        ignoreUnknownSignals: true
        function onPluginIdChanged() { root.restoreCountry() }
    }

    component Choice: ColumnLayout {
        property string fieldKey
        property string title
        property var options
        property string fallback
        Layout.fillWidth: true
        spacing: 4
        Label { text: parent.title; wrapMode: Text.Wrap; Layout.fillWidth: true }
        UiComboBox {
            id: combo
            objectName: "landscapeSetting_" + parent.fieldKey
            Layout.fillWidth: true
            model: parent.options.map(option => root.tr(option.label))
            currentIndex: Math.max(0, parent.options.findIndex(option => option.value === root.read(parent.fieldKey, parent.fallback)))
            onActivated: index => root.put(parent.fieldKey, parent.options[index].value)
        }
    }
    component Coordinate: ColumnLayout {
        property string fieldKey
        property string title
        property real limit
        Layout.fillWidth: true
        spacing: 4
        Label { text: parent.title; Layout.fillWidth: true; wrapMode: Text.Wrap }
        UiTextField {
            id: editor
            objectName: "landscapeSetting_" + parent.fieldKey
            Layout.fillWidth: true
            enabled: root.read("manualLocationEnabled", false)
            readonly property var draftValue: root.read(parent.fieldKey, 0)
            // 编辑中保留小数点和负号等中间态，不能每次草稿通知都格式化输入。
            function syncText() {
                if (!activeFocus) text = draftValue && typeof draftValue === "object"
                    ? String(draftValue.invalidCoordinate || "") : String(draftValue)
            }
            onDraftValueChanged: syncText()
            onActiveFocusChanged: syncText()
            Component.onCompleted: syncText()
            invalid: !root.validCoordinate(text, parent.limit)
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onTextEdited: {
                root.customCoordinates = true
                // 非数值草稿保证 NaN/Infinity 等文本也被 schema 拒绝；保留原文供继续编辑。
                root.put(parent.fieldKey, root.validCoordinate(text, parent.limit) ? Number(text) : {invalidCoordinate: text})
            }
        }
        Label {
            visible: editor.invalid
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: editor.dangerColor
            text: root.tr("请输入 %1 到 %2 之间的数字").arg(-parent.limit).arg(parent.limit)
        }
    }

    ColumnLayout {
        id: body
        width: parent.width
        spacing: 16
        UiCard {
            Layout.fillWidth: true
            title: root.tr("风景")
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 520 ? 2 : 1
                columnSpacing: 16; rowSpacing: 12
                Choice {
                    fieldKey: "scene"; title: root.tr("场景"); fallback: "cabin"
                    options: [{value:"cabin",label:"山间小屋"},{value:"lighthouse",label:"海边灯塔"},
                        {value:"street",label:"夜雨街角"},{value:"campsite",label:"湖畔营地"},
                        {value:"railway",label:"田野铁道"},{value:"island",label:"空中浮岛"},
                        {value:"window_city",label:"窗外城市"},{value:"below_surface",label:"水面以下"},
                        {value:"westward_journey",label:"取经路上"}]
                }
                Choice {
                    fieldKey: "timeMode"; title: root.tr("时段"); fallback: "auto"
                    options: [{value:"auto",label:"跟随当地太阳"},{value:"dawn",label:"清晨"},
                        {value:"day",label:"白天"},{value:"dusk",label:"黄昏"},{value:"night",label:"夜晚"}]
                }
                Choice {
                    fieldKey: "weather"; title: root.tr("天气"); fallback: "clear"
                    options: [{value:"clear",label:"晴天"},{value:"rain",label:"小雨"},{value:"snow",label:"飘雪"}]
                }
                Choice {
                    fieldKey: "frameStyle"; title: root.tr("画框样式"); fallback: "wood"
                    options: [{value:"none",label:"无画框"},{value:"thin",label:"极简细框"},{value:"wood",label:"暖木框"},
                        {value:"mat",label:"白色卡纸框"},{value:"metal",label:"像素金属框"}]
                }
                Choice {
                    fieldKey: "goldenHourMode"; title: root.tr("朝阳与晚霞"); fallback: "random"
                    options: [{value:"random",label:"随机出现（每天 1/3 概率）"},
                        {value:"always",label:"每天出现"},{value:"off",label:"关闭"}]
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: root.tr("仅晴天黄金时段生效，朝夕共用当天随机结果。选择每天出现，再切换清晨或黄昏可预览。")
            }
            Switch {
                text: root.tr("动态效果"); checked: root.read("animationsEnabled", true)
                onToggled: root.put("animationsEnabled", checked)
            }
            Switch {
                text: root.tr("偶发流星"); checked: root.read("rareEventsEnabled", true)
                onToggled: root.put("rareEventsEnabled", checked)
            }
        }
        UiCard {
            Layout.fillWidth: true
            title: root.tr("地理位置")
            Choice {
                fieldKey: "locationSource"; title: root.tr("定位方式"); fallback: "system"
                options: [{value:"system",label:"Windows 定位优先"},{value:"manual",label:"仅使用手动位置"}]
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: root.tr("选择城市会启用手动位置并填入近似坐标。系统定位优先时，该位置仅作备用；仅使用手动位置时，始终按所选坐标计算。")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 520 ? 2 : 1
                columnSpacing: 16; rowSpacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: root.tr("国家／地区") }
                    UiComboBox {
                        objectName: "landscapeCountry"
                        Layout.fillWidth: true
                        model: root.countries.map(country => root.tr(country.label))
                        currentIndex: root.countryIndex
                        onActivated: index => { root.countryIndex = index; root.customCoordinates = false }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: root.tr("城市") }
                    UiComboBox {
                        objectName: "landscapeCity"
                        Layout.fillWidth: true
                        model: [root.tr("自定义经纬度")].concat(root.cities.map(city => root.tr(city.label)))
                        currentIndex: root.matchedCity + 1
                        onActivated: index => root.chooseCity(index)
                    }
                }
            }
            Switch {
                objectName: "landscapeManualEnabled"
                Layout.fillWidth: true
                text: root.tr("启用手动位置")
                checked: root.read("manualLocationEnabled", false)
                onToggled: root.enableManual(checked)
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= 520 ? 2 : 1
                columnSpacing: 16; rowSpacing: 12
                Coordinate { fieldKey: "latitude"; title: root.tr("纬度（北纬为正）"); limit: 90 }
                Coordinate { fieldKey: "longitude"; title: root.tr("经度（东经为正）"); limit: 180 }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: root.tr("修改经纬度可使用任意地点。日出日落仍按电脑时区显示；保存后生效。")
            }
        }
    }
}
