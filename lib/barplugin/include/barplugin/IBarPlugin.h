// IBarPlugin.h — 插件统一接口（取代 QWidget* 方案）
// 接口只返回 QObject* viewModel 与 QUrl qmlUrl；实例化 QML 由核心统一做。
#pragma once
#include <QObject>
#include <QUrl>

#include "barplugin/PluginMetadata.h"
#include "barplugin/BarPluginContext.h"

namespace xuanhub {

class IBarPlugin {
public:
    virtual ~IBarPlugin() = default;

    virtual PluginMetadata metadata() const = 0;

    // 初始化；ctx 由核心注入。返回 false 则加载失败。
    virtual bool init(BarPluginContext* ctx) = 0;

    // 暴露给 QML 的数据/行为模型，可返回 nullptr。
    virtual QObject* viewModel() = 0;

    // 插件自带的 QML 文件，相对插件 DLL 目录（如 "Clock.qml"），
    // 核心拼成绝对 file URL 后交给 Loader。
    virtual QUrl qmlUrl() const = 0;

    virtual void shutdown() = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPlugin, "xuanhub.BarPlugin/1.0")
