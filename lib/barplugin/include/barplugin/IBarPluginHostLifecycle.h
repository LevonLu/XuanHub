// IBarPluginHostLifecycle.h — host 子插件按 ID 销毁的可选生命周期扩展
#pragma once

#include <QtPlugin>
#include <QString>

namespace xuanhub {

// 与 IBarPluginHost 分离，避免强迫尚未迁移的第三方 host 立即实现增量销毁。
// 宿主未实现本接口时，核心仍可回退为重启整个 host。
class IBarPluginHostLifecycle {
public:
    virtual ~IBarPluginHostLifecycle() = default;
    virtual bool destroySubPlugin(const QString& id) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostLifecycle,
                    "xuanhub.BarPluginHostLifecycle/1.0")
