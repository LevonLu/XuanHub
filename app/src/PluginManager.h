// PluginManager.h — 插件实例、作用域与运行时调用的统一管理
// 发现交由 PluginCatalog，配置协调交由 PluginReconciler；停止前同步发射
// pluginsAboutToStop 解除 QML 引用，再按依赖闭包执行 shutdown/unload。
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

#include "PluginConfigTracker.h"
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

    enum class ConfigApplyResult { Applied, Pending, Unsupported, Rejected };
    struct ConfigValidationResult {
        bool supported = false;
        bool valid = true;
        QString error;
    };
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
        QVector<xuanhub::PluginAction> actions;
        xuanhub::PluginLayoutPreference layout;
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
    ConfigValidationResult validateConfig(const QString& id,
                                          const QVariantMap& config) const;
    ConfigApplyResult applyConfig(const PluginConfigEntry& entry);
    QStringList dependentPluginIds(const QString& id) const;
    const LoadedPlugin* loadedPlugin(const QString& id) const;
    bool isLoaded(const QString& id) const { return m_loadedIds.contains(id); }
    QString loadError(const QString& id) const { return m_loadErrors.value(id); }
    const QHash<QString, QString>& loadErrors() const { return m_loadErrors; }
    const ConfigMigrationSuggestion* migrationSuggestion(const QString& id) const;
    Q_INVOKABLE QVariantMap invokeAction(const QString& pluginId,
                                         const QString& actionId);
    QVariantMap dependencyInstallPlan(const QString& pluginId) const;
    bool installPluginDependencies(const QString& pluginId);

    const QVector<LoadedPlugin>& loaded() const { return m_loaded; }
    QList<PluginConfigEntry> configEntries() const { return m_configTracker.entries(); }
    QVariantMap configStates() const { return m_configTracker.states(); }
    void shutdownAll();   // 重载周期约定：必须先 shutdownAll() 再 loadFrom()（入口有重入保护）

Q_SIGNALS:
    void pluginsChanged();
    void configStatesChanged();
    void pluginsAboutToStop(const QStringList& ids);
    void dependencyInstallFinished(const QString& pluginId, bool success,
                                   const QString& error);

private:
    Q_SLOT void onHostedConfigInitialized(const QString& id, bool success,
                                          const QString& error);
    Q_SLOT void onHostedConfigApplied(const QString& id, const QVariantMap& config,
                                      bool success, const QString& error);
    Q_SLOT void onHostedMigrationReady(const QString& id, int currentVersion,
                                       int targetVersion, const QVariantMap& config);
    Q_SLOT void onHostedMigrationFailed(const QString& id, int currentVersion,
                                        int targetVersion, const QString& error);
    Q_SLOT void onHostedDependencyInstallFinished(const QString& id, bool success,
                                                  const QString& error);
    void loadFromImpl(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled,
                      bool allowExisting);
    bool createHostedPlugin(const QString& hostId, xuanhub::IBarPluginHost* host,
                            const PluginConfigEntry& entry);
    bool stopInstance(const QString& id);
    bool unloadLibrary(const QString& id, QPluginLoader* loader);
    bool m_deferUnloading = false;
    QVector<QPair<QString, QPluginLoader*>> m_deferredLoaders;
    bool m_stopping = false;
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
    QHash<QString, QString> m_candidateHostBySubPluginId;    // 已发现子插件（含加载失败）→ host id
    QHash<QString, QString> m_subPluginHostId;               // 子插件 id → host id
    QHash<QString, xuanhub::PluginEventScope*> m_scopeById;  // 原生插件/host id → 作用域
    QSet<QString> m_loadedIds;   // 按 metadata.id 去重
    QHash<QString, QString> m_unloadErrors; // 卸载失败后禁止在本进程重新初始化
    QHash<QString, QString> m_loadErrors; // 未实例化/初始化失败也向设置页暴露原因
    QHash<QString, ConfigMigrationSuggestion> m_migrationSuggestions;
    PluginConfigTracker m_configTracker;
    bool m_shut = false;
};
