// IBarPluginHostEventBus.h — host 插件接收子插件独立 EventBus 作用域的可选接口
#pragma once

#include <QtPlugin>
#include <QString>

namespace xuanhub {

class PluginEventScope;

class IBarPluginHostEventBus {
public:
    virtual ~IBarPluginHostEventBus() = default;
    virtual bool bindSubPluginEventScope(const QString& id,
                                         PluginEventScope* eventScope) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostEventBus,
                    "xuanhub.BarPluginHostEventBus/1.0")
