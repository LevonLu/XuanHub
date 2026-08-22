// IBarPluginHost.h — 可选宿主接口：一个插件 DLL 承载多个子插件（M6）
// 典型实现：pybridge 扫描 plugins_py/，为每个 Python 子插件创建代理 ViewModel。
// 两阶段契约：init(ctx) 之后核心先调 subPluginIds() 枚举，按 config 过滤/去重，
// 再对启用的子插件调 createSubPlugin()（此时才传入该子插件的 config 段）。
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "barplugin/PluginMetadata.h"
#include "barplugin/PluginDescriptor.h"

namespace xuanhub {

struct SubPluginInfo {
    PluginMetadata meta;
    QObject* viewModel = nullptr; // 由 host 创建并持有，随 host shutdown 销毁
    QString qmlFilePath;          // 子插件 QML 文件名（相对 dirPath）
    QString dirPath;              // 子插件目录绝对路径（核心用它拼 qmlUrl）
    QStringList capabilities;     // 追加字段保持旧 host 源码兼容
    QStringList permissions;      // 隔离子插件实际授权以前置 Manifest 为准
    QVector<PluginAction> actions; // 声明式动作；宿主按 ID 校验后才允许调用
    PluginLayoutPreference layout; // 可选响应式布局偏好；缺省保持旧固定行为
};

class IBarPluginHost {
public:
    virtual ~IBarPluginHost() = default;

    // 枚举发现的全部子插件 id（不管是否启用，过滤是核心的事）
    virtual QStringList subPluginIds() = 0;

    // 为指定子插件创建实例；cfg 为 config.json 中该子插件的 config 段。
    // 失败返回 viewModel == nullptr 的 SubPluginInfo（核心记日志后跳过）。
    virtual SubPluginInfo createSubPlugin(const QString& id, const QVariantMap& cfg) = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPluginHost, "xuanhub.BarPluginHost/1.0")
