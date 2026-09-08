// PyBridgePlugin.h — Python 桥接宿主插件（进程隔离版）
// IBarPlugin2 外壳 + IBarPluginHost：扫描 plugins_py，并为每个子插件创建独立 worker 代理。
#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include "barplugin/IBarPlugin2.h"
#include "barplugin/IBarPluginHost.h"
#include "barplugin/IBarPluginHostLifecycle.h"
#include "barplugin/IBarPluginHostConfigurable.h"
#include "barplugin/IBarPluginHostConfigMigration.h"
#include "barplugin/IBarPluginHostConfigValidation.h"
#include "barplugin/IBarPluginHostDiagnostics.h"
#include "barplugin/IBarPluginHostDependencies.h"
#include "barplugin/IBarPluginHostEventBus.h"
#include "barplugin/IBarPluginHostServices.h"

class QProcess;

class PyBridgePlugin : public QObject, public xuanhub::IBarPlugin2,
                       public xuanhub::IBarPluginHost,
                       public xuanhub::IBarPluginHostLifecycle,
                       public xuanhub::IBarPluginHostConfigurable,
                       public xuanhub::IBarPluginHostConfigMigration,
                       public xuanhub::IBarPluginHostConfigValidation,
                       public xuanhub::IBarPluginHostDiagnostics,
                       public xuanhub::IBarPluginHostDependencies,
                       public xuanhub::IBarPluginHostEventBus,
                       public xuanhub::IBarPluginHostServices {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2 xuanhub::IBarPluginHost
                 xuanhub::IBarPluginHostLifecycle
                 xuanhub::IBarPluginHostConfigurable
                 xuanhub::IBarPluginHostConfigMigration
                 xuanhub::IBarPluginHostConfigValidation
                 xuanhub::IBarPluginHostDiagnostics
                 xuanhub::IBarPluginHostDependencies
                 xuanhub::IBarPluginHostEventBus
                 xuanhub::IBarPluginHostServices)
public:
    // IBarPlugin2
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    xuanhub::PluginConfigValidationResult validateConfig(const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap& config) override;
    QObject* viewModel() override;   // 宿主自身无可视界面，返回 nullptr
    QUrl qmlUrl() const override;    // 宿主自身无 QML
    void shutdown() override;        // 关闭子插件并收口进程回收，保证 DLL 可安全卸载

    // IBarPluginHost（两阶段：先枚举 id，核心按 config 过滤后再实例化）
    QStringList subPluginIds() override;
    xuanhub::SubPluginInfo createSubPlugin(const QString& id, const QVariantMap& cfg) override;
    bool destroySubPlugin(const QString& id) override;
    bool canApplySubPluginConfig(const QString& id) const override;
    bool applySubPluginConfig(const QString& id, const QVariantMap& config) override;
    void setSubPluginConfigVersion(const QString& id,
                                   int currentVersion) override;
    xuanhub::HostedPluginConfigValidationResult validateSubPluginConfig(
        const QString& id, const QVariantMap& config, int timeoutMs) override;
    bool bindSubPluginHostServices(const QString& id,
                                   xuanhub::IHostServices* services) override;
    bool bindSubPluginEventScope(const QString& id,
                                 xuanhub::PluginEventScope* eventScope) override;
    QString subPluginLoadError(const QString& id) const override;
    QVariantMap subPluginDependencyPlan(const QString& id) const override;
    bool installSubPluginDependencies(const QString& id) override;

Q_SIGNALS:
    void subPluginDependencyInstallFinished(const QString& id, bool success,
                                            const QString& error);

private:
    xuanhub::PluginContext2 m_ctx;     // 服务借用到 shutdown；配置与目录按值保存
    bool m_initialized = false;
    QHash<QString, QString> m_dirs;    // id → 子插件目录（subPluginIds 扫描结果）
    QHash<QString, QJsonObject> m_manifests; // id → 已通过预检的 plugin.json
    QHash<QString, int> m_configVersions; // id → config.json 当前版本
    QString m_pythonExe;               // 已验证的 python.exe；worker 由它启动
    QString m_workerScript;             // <exeDir>/plugins_py/bridge_worker.py
    QString m_doctorScript;             // <exeDir>/plugins_py/dependency_doctor.py
    QString m_installerScript;          // 用户确认后才执行的离线环境安装器
    QHash<QString, QObject*> m_vms;     // id → 隔离代理 VM，可单独销毁或 shutdown 全量回收
    QHash<QString, QString> m_subPluginErrors; // 实例化前 doctor/Manifest 的确定错误
    QHash<QString, QVariantMap> m_dependencyPlans; // 含私有内部路径，仅接口返回时裁剪
    QHash<QString, QProcess*> m_dependencyInstallers; // 每插件最多一个异步安装器
};
