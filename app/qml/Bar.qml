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
