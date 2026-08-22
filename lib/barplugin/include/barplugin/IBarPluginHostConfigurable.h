// IBarPluginHostConfigurable.h — host 子插件原地热配置的可选扩展接口
#pragma once

#include <QtPlugin>
#include <QString>
#include <QVariantMap>

namespace xuanhub {

class IBarPluginHostConfigurable {
public:
    virtual ~IBarPluginHostConfigurable() = default;
    virtual bool canApplySubPluginConfig(const QString& id) const = 0;
    virtual bool applySubPluginConfig(const QString& id, const QVariantMap& config) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostConfigurable,
                    "xuanhub.BarPluginHostConfigurable/1.0")
