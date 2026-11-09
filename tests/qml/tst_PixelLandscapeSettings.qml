// tst_PixelLandscapeSettings.qml — 城市选择、草稿保留与自定义坐标交互
import QtQuick
import QtTest
import XuanHub
import "../../plugins/pixel_landscape" as Landscape
import "../../plugins/pixel_landscape/CityPresets.js" as Cities

TestCase {
    id: tests
    name: "PixelLandscapeSettings"
    when: windowShown
    visible: true
    width: 700; height: 1100
    QtObject {
        id: draft
        property string pluginId: "pixel_landscape"
        property var values: ({})
        property int revision: 0
        function value(key) { return values[key] }
        function setValue(key, value) {
            const next = Object.assign({}, values); next[key] = value
            values = next; ++revision
        }
    }
    Component { id: settings; Landscape.PixelLandscapeSettings {} }
    function makePage(values, width) {
        draft.pluginId = "pixel_landscape"; draft.values = values; draft.revision = 0
        const page = createTemporaryObject(settings, tests, {width: width || 620})
        verify(page !== null); page.plugin = draft
        verify(waitForRendering(page))
        return page
    }
    function select(combo, index) {
        // 从真实 ComboBox 的键盘路径提交选项，覆盖绑定与模型重置的时序。
        combo.forceActiveFocus()
        keyClick(Qt.Key_Home)
        for (let i = 0; i < index; ++i) keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
    }
    function test_catalog_is_valid_and_domestic_coverage() {
        compare(Cities.countries.length, 13)
        compare(Cities.countries[0].id, "CN")
        verify(Cities.countries[0].cities.length >= 45)
        const ids = {}; let count = 0
        for (const country of Cities.countries) for (const city of country.cities) {
            verify(!ids[city.id]); ids[city.id] = true; ++count
            verify(isFinite(city.latitude) && Math.abs(city.latitude) <= 90)
            verify(isFinite(city.longitude) && Math.abs(city.longitude) <= 180)
        }
        compare(count, 75)
        compare(Cities.findCity(Cities.countries[0].cities, undefined, undefined), -1)
    }
    function test_opening_old_config_does_not_write_defaults() {
        const values = {scene:"island", timeMode:"dusk", weather:"snow", frameStyle:"metal", unknown:42}
        const page = makePage(values)
        compare(draft.revision, 0)
        compare(findChild(page,"landscapeSetting_scene").currentIndex, 5)
        compare(findChild(page,"landscapeSetting_timeMode").currentIndex, 3)
        compare(findChild(page,"landscapeSetting_weather").currentIndex, 2)
        compare(findChild(page,"landscapeSetting_frameStyle").currentIndex, 4)
        compare(findChild(page,"landscapeCity").currentIndex, 0)
        compare(draft.value("latitude"), undefined)
        compare(draft.value("manualLocationEnabled"), undefined)
    }
    function test_country_and_city_fill_only_location_draft() {
        const page = makePage({locationSource:"system",scene:"street",weather:"rain",unknown:42})
        const country = findChild(page,"landscapeCountry")
        select(country, 1)
        compare(page.countryIndex, 1); compare(draft.revision, 0)
        const city = findChild(page,"landscapeCity")
        select(city, 1)
        compare(draft.value("latitude"), Cities.countries[1].cities[0].latitude)
        compare(draft.value("longitude"), Cities.countries[1].cities[0].longitude)
        compare(draft.value("manualLocationEnabled"), true)
        compare(draft.value("locationSource"), "system")
        compare(draft.value("scene"), "street"); compare(draft.value("weather"), "rain")
        compare(draft.value("unknown"), 42)
        const lat = draft.value("latitude"), lon = draft.value("longitude")
        select(country, 8)
        compare(draft.value("latitude"), lat); compare(draft.value("longitude"), lon)
        compare(city.currentIndex, 0)
    }
    function test_reopen_recognizes_city_and_edit_keeps_invalid_draft() {
        const city = Cities.countries[8].cities[1]
        const page = makePage({manualLocationEnabled:true,locationSource:"manual",latitude:city.latitude,longitude:city.longitude}, 360)
        compare(page.countryIndex, 8)
        compare(findChild(page,"landscapeCity").currentIndex, 2)
        const lat = findChild(page,"landscapeSetting_latitude")
        lat.forceActiveFocus(); keyClick(Qt.Key_A, Qt.ControlModifier); keyClick(Qt.Key_9); keyClick(Qt.Key_1)
        compare(lat.invalid, true); compare(draft.value("latitude").invalidCoordinate, "91")
        compare(findChild(page,"landscapeCity").currentIndex, 0)
        keyClick(Qt.Key_A, Qt.ControlModifier); keyClick(Qt.Key_3); keyClick(Qt.Key_0); keyClick(Qt.Key_Period); keyClick(Qt.Key_5)
        compare(lat.invalid, false); compare(draft.value("latitude"), 30.5)
        compare(draft.value("longitude"), city.longitude)
        for (const width of [360,620]) {
            page.width = width; verify(waitForRendering(page))
            for (const name of ["landscapeCountry","landscapeCity","landscapeSetting_latitude","landscapeSetting_longitude"]) {
                const item = findChild(page,name), point = item.mapToItem(page,0,0)
                verify(point.x >= 0 && point.x + item.width <= width + 0.5)
            }
        }
        // 代理切换到别的插件时，旧页面上的迟到操作不得写错草稿。
        const revision = draft.revision; draft.pluginId = "sysinfo"; page.chooseCity(1)
        compare(draft.revision, revision)
    }
}
