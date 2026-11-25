// Bar.qml — Theme Schema v2 外壳宿主；可信主题扩展失败时自动回退内置外壳。
import QtQuick
import XuanHub

Item {
    id: host

    readonly property string barMode: barConfig && barConfig.barMode
                                      ? barConfig.barMode : "appbar"
    readonly property bool customAllowed: themeManager && themeManager.customChrome
                                          && themeManager.chromeSupportsMode(barMode)
    property bool customContractValid: false
    readonly property var activeChrome: customLoader.status === Loader.Ready
                                        && customContractValid
                                        ? customLoader.item : fallbackChrome
    implicitWidth: activeChrome ? activeChrome.implicitWidth : 0
    readonly property real maxScrollX: barMode === "taskbar"
                                      ? Math.max(0, implicitWidth - width) : 0
    property real scrollX: 0
    clip: maxScrollX > 0
    onMaxScrollXChanged: scrollX = Math.min(scrollX, maxScrollX)
    onScrollXChanged: {
        // 滚动后锚点可能已经离开可视区，关闭原位置上的插件弹窗。
        if (popupService && popupService.activePopup)
            popupService.activePopup.visible = false
    }

    function injectChrome(item) {
        if (!item) return false
        try {
            item.hostPluginModel = pluginModel
            item.hostTheme = themeManager
            item.hostBarConfig = barConfig
        } catch (error) {
            console.warn("[Theme] barChrome 根对象必须声明 hostPluginModel、hostTheme、hostBarConfig")
            return false
        }
        return true
    }

    Item {
        // 原生子窗口只占可用空间；主题始终按完整内容排版，滚动不会反过来改变上报宽度。
        x: -host.scrollX
        width: host.width + host.maxScrollX
        height: host.height

        StandardBarChrome {
            id: fallbackChrome
            anchors.fill: parent
            visible: customLoader.status !== Loader.Ready || !host.customContractValid
            hostPluginModel: pluginModel
            hostTheme: themeManager
            hostBarConfig: barConfig
        }

        Loader {
            id: customLoader
            anchors.fill: parent
            active: host.customAllowed
            source: active ? themeManager.chromeUrl : ""
            visible: status === Loader.Ready && host.customContractValid

            onSourceChanged: host.customContractValid = false
            onLoaded: host.customContractValid = host.injectChrome(item)
            onStatusChanged: {
                if (status === Loader.Error) {
                    host.customContractValid = false
                    console.warn("[Theme] barChrome 加载失败，已回退内置外壳:", source)
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: host.maxScrollX > 0
        acceptedButtons: Qt.NoButton
        onWheel: wheel => {
            const pixels = wheel.pixelDelta
            const hasPixels = pixels.x !== 0 || pixels.y !== 0
            const delta = hasPixels ? pixels : wheel.angleDelta
            const step = Math.abs(delta.x) > Math.abs(delta.y) ? delta.x : delta.y
            // 触控板保留像素位移；普通滚轮每一格（120 个角度单位）横移 48 个逻辑像素。
            host.scrollX = Math.max(0, Math.min(host.maxScrollX,
                                               host.scrollX - step * (hasPixels ? 1 : 48 / 120)))
            wheel.accepted = true
        }
    }
}
