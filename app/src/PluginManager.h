// PluginManager.h — 插件扫描/加载/生命周期
// 扫描 plugins/<id>/ 下的 *.dll，校验 IID 后 qobject_cast<IBarPlugin*>，
// 按 config 启用列表 init，收集 viewModel + qmlUrl 供渲染层使用。
// 生命周期约定：shutdownAll() 先调各插件 shutdown()（删光 DLL 出身的对象），
// 再 unload()；load hints 已清 PreventUnload，unload 会删除根实例并真正卸载 DLL。
// 支持 shutdownAll() → loadFrom() 的热重载周期（调用方需先重置 QML 模型）。
#pragma once
#include <QObject>
#include <QVector>
#include <QSet>
#include <QUrl>
#include <QPluginLoader>
#include <QHash>

#include <memory>
#include <vector>

#include "barplugin/IBarPlugin.h"
#include "barplugin/PluginHealth.h"

struct PluginConfigEntry;
namespace xuanhub { class EventBus; }
namespace xuanhub { class PluginEventScope; }
namespace xuanhub { class IBarPluginHost; }

class PluginManager : public QObject {
    Q_OBJECT
public:
    struct LoadedPlugin {
        QString id;
        QString position;            // left/center/right
        int order = 0;
        int width = 0;               // 插件宽度（像素，0=跟随内容自适应）
        QObject* viewModel = nullptr;
        QUrl qmlUrl;                 // 绝对 file URL
        xuanhub::IBarPlugin* iface = nullptr;
        xuanhub::PluginHealth health;
    };

    PluginManager(xuanhub::EventBus* bus, const QString& configDir, QObject* parent = nullptr);
    ~PluginManager() override;

    void loadFrom(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled);
    bool loadOne(const QString& pluginsDir, const PluginConfigEntry& entry);
    bool stopOne(const QString& id);
    bool restartOne(const QString& pluginsDir, const PluginConfigEntry& entry);
    QStringList dependentPluginIds(const QString& id) const;
    const LoadedPlugin* loadedPlugin(const QString& id) const;

    const QVector<LoadedPlugin>& loaded() const { return m_loaded; }
    void shutdownAll();   // 重载周期约定：必须先 shutdownAll() 再 loadFrom()（入口有重入保护）

private:
    void loadFromImpl(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled,
                      bool allowExisting);
    bool createHostedPlugin(const QString& hostId, xuanhub::IBarPluginHost* host,
                            const PluginConfigEntry& entry);

    xuanhub::EventBus* m_bus;
    QString m_configDir;
    QVector<LoadedPlugin> m_loaded;
    QVector<QPluginLoader*> m_loaders;
    // host 插件（IBarPluginHost，如 pybridge）：shutdownAll 时最后 shutdown，
    // 其子插件 VM 依赖 host 持有的运行时（如 CPython 解释器）
    QVector<xuanhub::IBarPlugin*> m_hosts;
    std::vector<std::unique_ptr<xuanhub::PluginEventScope>> m_eventScopes;
    QHash<QString, QPluginLoader*> m_loaderById;             // 原生插件/host id → loader
    QHash<QString, xuanhub::IBarPlugin*> m_ifaceById;        // 原生插件/host id → 接口
    QHash<QString, xuanhub::IBarPluginHost*> m_hostById;     // host id → host 接口
    QHash<QString, QString> m_subPluginHostId;               // 子插件 id → host id
    QHash<QString, xuanhub::PluginEventScope*> m_scopeById;  // 原生插件/host id → 作用域
    QSet<QString> m_loadedIds;   // 按 metadata.id 去重
    bool m_shut = false;
};
