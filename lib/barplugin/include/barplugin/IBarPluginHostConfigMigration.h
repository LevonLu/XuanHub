// IBarPluginHostConfigMigration.h — Host 子插件配置版本注入接口
#pragma once

#include <QObject>
#include <QString>

namespace xuanhub {

// IBarPluginHost V1 只接收 config map，无法传递条目级 configVersion；宿主插件按需实现
// 此独立接口，在 createSubPlugin() 前接收已校验的当前版本，避免修改既有虚函数表。
class IBarPluginHostConfigMigration {
public:
    virtual ~IBarPluginHostConfigMigration() = default;
    virtual void setSubPluginConfigVersion(const QString& id,
                                           int currentVersion) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostConfigMigration,
                    "xuanhub.BarPluginHostConfigMigration/1.0")
