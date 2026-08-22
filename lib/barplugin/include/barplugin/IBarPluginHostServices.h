// IBarPluginHostServices.h — host 子插件绑定核心身份服务的可选扩展接口
#pragma once

#include <QObject>
#include <QString>

namespace xuanhub {
class IHostServices;

class IBarPluginHostServices {
public:
    virtual ~IBarPluginHostServices() = default;
    virtual bool bindSubPluginHostServices(const QString& id,
                                           IHostServices* services) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHostServices,
                    "xuanhub.BarPluginHostServices/1.0")
