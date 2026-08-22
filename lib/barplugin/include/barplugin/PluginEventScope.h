// PluginEventScope.h — 插件拥有的 EventBus 订阅作用域
#pragma once

#include <QString>

#include "barplugin/barplugin_export.h"

class QObject;

namespace xuanhub {

class EventBus;

class BARPLUGIN_EXPORT PluginEventScope final {
public:
    explicit PluginEventScope(EventBus* bus);
    ~PluginEventScope();

    PluginEventScope(const PluginEventScope&) = delete;
    PluginEventScope& operator=(const PluginEventScope&) = delete;

    bool subscribe(const QString& topic, QObject* receiver, const char* slot);
    // clear 后作用域永久关闭，拒绝新订阅；用于 shutdown 前切断跨 DLL 回调。
    void clear();
    bool active() const { return m_active; }

private:
    EventBus* m_bus = nullptr;
    quint64 m_ownerToken = 0;
    bool m_active = true;
};

} // namespace xuanhub
