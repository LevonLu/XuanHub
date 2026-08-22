// IBarPluginHostDependencies.h — host 子插件依赖计划与确认安装的可选接口
#pragma once

#include <QString>
#include <QVariantMap>

namespace xuanhub {

class IBarPluginHostDependencies {
public:
    virtual ~IBarPluginHostDependencies() = default;

    virtual QVariantMap subPluginDependencyPlan(const QString& id) const = 0;
    virtual bool installSubPluginDependencies(const QString& id) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostDependencies,
                    "xuanhub.BarPluginHostDependencies/1.0")
