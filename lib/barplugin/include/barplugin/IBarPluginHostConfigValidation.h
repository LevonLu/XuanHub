// IBarPluginHostConfigValidation.h — Host 子插件保存前配置验证扩展接口
#pragma once

#include <QtPlugin>
#include <QString>
#include <QVariantMap>

namespace xuanhub {

struct HostedPluginConfigValidationResult {
    bool supported = false;
    bool valid = true;
    QString error;
};

// 独立接口避免修改既有 IBarPluginHost/IBarPluginHostConfigurable 虚函数表。
// 实现可在隔离进程中验证，但不得应用配置或写入持久化状态。
class IBarPluginHostConfigValidation {
public:
    virtual ~IBarPluginHostConfigValidation() = default;
    virtual HostedPluginConfigValidationResult validateSubPluginConfig(
        const QString& id, const QVariantMap& config, int timeoutMs) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostConfigValidation,
                    "xuanhub.BarPluginHostConfigValidation/1.0")
