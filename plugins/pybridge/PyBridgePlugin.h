// PyBridgePlugin.h — Python 桥接宿主插件（进程隔离版）
// IBarPlugin 外壳 + IBarPluginHost：扫描 plugins_py，并为每个子插件创建独立 worker 代理。
#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPluginHost.h"
#include "barplugin/IBarPluginHostLifecycle.h"
#include "barplugin/IBarPluginHostConfigurable.h"
#include "barplugin/IBarPluginHostConfigMigration.h"
#include "barplugin/IBarPluginHostConfigValidation.h"
#include "barplugin/IBarPluginHostEventBus.h"
#include "barplugin/IBarPluginHostServices.h"

class PyBridgePlugin : public QObject, public xuanhub::IBarPlugin,
                       public xuanhub::IBarPluginHost,
                       public xuanhub::IBarPluginHostLifecycle,
                       public xuanhub::IBarPluginHostConfigurable,
                       public xuanhub::IBarPluginHostConfigMigration,
                       public xuanhub::IBarPluginHostConfigValidation,
                       public xuanhub::IBarPluginHostEventBus,
                       public xuanhub::IBarPluginHostServices {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin xuanhub::IBarPluginHost
                 xuanhub::IBarPluginHostLifecycle
                 xuanhub::IBarPluginHostConfigurable
                 xuanhub::IBarPluginHostConfigMigration
                 xuanhub::IBarPluginHostConfigValidation
                 xuanhub::IBarPluginHostEventBus
                 xuanhub::IBarPluginHostServices)
public:
    // IBarPlugin
    xuanhub::PluginMetadata metadata() const override;
    bool init(xuanhub::BarPluginContext* ctx) override;
    QObject* viewModel() override;   // 宿主自身无可视界面，返回 nullptr
    QUrl qmlUrl() const override;    // 宿主自身无 QML
    void shutdown() override;        // 异步终止全部子插件 worker；不等待，避免卡住 GUI

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

private:
    xuanhub::BarPluginContext m_ctx;   // 拷贝保存（init 传入的 ctx 是核心栈上对象）
    QHash<QString, QString> m_dirs;    // id → 子插件目录（subPluginIds 扫描结果）
    QHash<QString, QJsonObject> m_manifests; // id → 已通过预检的 plugin.json
    QHash<QString, int> m_configVersions; // id → config.json 当前版本
    QString m_pythonExe;               // 已验证的 python.exe；worker 由它启动
    QString m_workerScript;             // <exeDir>/plugins_py/bridge_worker.py
    QHash<QString, QObject*> m_vms;     // id → 隔离代理 VM，可单独销毁或 shutdown 全量回收
};
