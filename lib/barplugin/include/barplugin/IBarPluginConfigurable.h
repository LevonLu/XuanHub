// IBarPluginConfigurable.h — 插件运行时校验并应用配置的可选扩展接口
#pragma once

#include <QtPlugin>
#include <QString>
#include <QVariantMap>

namespace xuanhub {

class IBarPluginConfigurable {
public:
    virtual ~IBarPluginConfigurable() = default;
    // 空字符串表示合法；非空内容是面向日志/设置页的拒绝原因。
    virtual QString validateConfig(const QVariantMap& config) const = 0;
    virtual bool applyConfig(const QVariantMap& config) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginConfigurable,
                    "xuanhub.BarPluginConfigurable/1.0")
