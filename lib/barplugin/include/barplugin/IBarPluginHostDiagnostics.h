// IBarPluginHostDiagnostics.h — host 子插件实例化失败时提供确定诊断的可选接口
#pragma once

#include <QString>

namespace xuanhub {

class IBarPluginHostDiagnostics {
public:
    virtual ~IBarPluginHostDiagnostics() = default;

    virtual QString subPluginLoadError(const QString& id) const = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostDiagnostics,
                    "xuanhub.BarPluginHostDiagnostics/1.0")
