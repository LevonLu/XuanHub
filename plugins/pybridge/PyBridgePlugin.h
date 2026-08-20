// PyBridgePlugin.h — Python 桥接宿主插件（进程隔离版）
// IBarPlugin 外壳 + IBarPluginHost：扫描 plugins_py，并为每个子插件创建独立 worker 代理。
#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPluginHost.h"

class PyBridgePlugin : public QObject, public xuanhub::IBarPlugin, public xuanhub::IBarPluginHost {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin xuanhub::IBarPluginHost)
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

private:
    xuanhub::BarPluginContext m_ctx;   // 拷贝保存（init 传入的 ctx 是核心栈上对象）
    QHash<QString, QString> m_dirs;    // id → 子插件目录（subPluginIds 扫描结果）
    QString m_pythonExe;               // 已验证的 python.exe；worker 由它启动
    QString m_workerScript;             // <exeDir>/plugins_py/bridge_worker.py
    QList<QObject*> m_vms;              // 已实例化的隔离代理 VM，shutdown 统一销毁
};
