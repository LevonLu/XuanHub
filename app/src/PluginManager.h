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
#include <functional>
#include <vector>

#include "barplugin/IBarPlugin.h"
#include "barplugin/PluginHealth.h"
#include "barplugin/PluginDescriptor.h"
#include "barplugin/HostServices.h"

struct PluginConfigEntry;
namespace xuanhub { class EventBus; }
namespace xuanhub { class PluginEventScope; }
namespace xuanhub { class IBarPluginHost; }
class PluginRuntime;
class PluginHostServices;
class PluginContextObject;

class PluginManager : public QObject {
    Q_OBJECT
public:
    struct HostEnvironment {
        std::function<QString()> activeLocale;
        std::function<QVariantMap()> currentPalette;
        QObject* localeNotifier = nullptr;
        QObject* themeNotifier = nullptr;
    };

    enum class ConfigApplyResult { Applied, Unsupported, Rejected };
    struct ConfigMigrationSuggestion {
        int currentVersion = 1;
        int targetVersion = 1;
        QVariantMap config;
        QString error;

        bool ready() const {
            return error.isEmpty() && targetVersion > currentVersion;
        }
    };
    struct LoadedPlugin {
        QString id;
        int apiVersion = 1;          // SDK 接口版本，host 子插件暂按 V1 契约
        QStringList capabilities;
        QString position;            // left/center/right
        int order = 0;
        int width = 0;               // 插件宽度（像素，0=跟随内容自适应）
        QObject* viewModel = nullptr;
        QObject* pluginContext = nullptr; // 宿主拥有、与插件生命周期相同的 QML 服务代理
        QUrl qmlUrl;                 // 绝对 file URL
        xuanhub::PluginHealth health;
    };

    PluginManager(xuanhub::EventBus* bus, const QString& configDir,
                  HostEnvironment hostEnvironment = {},
                  QObject* parent = nullptr);
    ~PluginManager() override;

    void loadFrom(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled);
    bool loadOne(const QString& pluginsDir, const PluginConfigEntry& entry);
    bool stopOne(const QString& id);
    bool restartOne(const QString& pluginsDir, const PluginConfigEntry& entry);
    bool updateLayout(const PluginConfigEntry& entry);
    ConfigApplyResult applyConfig(const PluginConfigEntry& entry);
    QStringList dependentPluginIds(const QString& id) const;
    const LoadedPlugin* loadedPlugin(const QString& id) const;
    QString loadError(const QString& id) const { return m_loadErrors.value(id); }
    const QHash<QString, QString>& loadErrors() const { return m_loadErrors; }
    const ConfigMigrationSuggestion* migrationSuggestion(const QString& id) const;

    const QVector<LoadedPlugin>& loaded() const { return m_loaded; }
    void shutdownAll();   // 重载周期约定：必须先 shutdownAll() 再 loadFrom()（入口有重入保护）

private:
    void loadFromImpl(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled,
                      bool allowExisting);
    bool createHostedPlugin(const QString& hostId, xuanhub::IBarPluginHost* host,
                            const PluginConfigEntry& entry);
    void removeRuntime(PluginRuntime* runtime);
    std::unique_ptr<PluginHostServices> createHostServices(
        const QString& id, xuanhub::HostPluginTrustLevel trustLevel,
        const QStringList& declaredPermissions,
        const QStringList& grantedPermissions) const;
    void retainHostServices(const QString& id,
                            std::unique_ptr<PluginHostServices> services,
                            std::unique_ptr<PluginContextObject> qmlContext = {});
    void removeHostServices(const QString& id);
    void recordLoadError(const QString& id, const QString& error);

    xuanhub::EventBus* m_bus;
    QString m_configDir;
    HostEnvironment m_hostEnvironment;
    QVector<LoadedPlugin> m_loaded;
    QVector<QPluginLoader*> m_loaders;
    // host 插件（IBarPluginHost，如 pybridge）：shutdownAll 时最后 shutdown，
    // 其子插件 VM 依赖 host 持有的运行时（如 CPython 解释器）
    QVector<PluginRuntime*> m_hosts;
    QVector<PluginRuntime*> m_services; // 无 barItem 能力的 V2 后台插件
    std::vector<std::unique_ptr<PluginRuntime>> m_runtimes;
    std::vector<std::unique_ptr<xuanhub::PluginEventScope>> m_eventScopes;
    struct HostServiceScope {
        QString id;
        std::unique_ptr<PluginHostServices> services;
        std::unique_ptr<PluginContextObject> qmlContext;
    };
    std::vector<HostServiceScope> m_hostServiceScopes;
    QHash<QString, QPluginLoader*> m_loaderById;             // 原生插件/host id → loader
    QHash<QString, PluginRuntime*> m_runtimeById;            // 原生插件/host id → V1/V2 适配
    QHash<QString, xuanhub::PluginDescriptor> m_descriptorById; // 已加载依赖与能力快照
    QHash<QString, xuanhub::IBarPluginHost*> m_hostById;     // host id → host 接口
    QHash<QString, QString> m_subPluginHostId;               // 子插件 id → host id
    QHash<QString, xuanhub::PluginEventScope*> m_scopeById;  // 原生插件/host id → 作用域
    QSet<QString> m_loadedIds;   // 按 metadata.id 去重
    QHash<QString, QString> m_loadErrors; // 未实例化/初始化失败也向设置页暴露原因
    QHash<QString, ConfigMigrationSuggestion> m_migrationSuggestions;
    bool m_shut = false;
};
