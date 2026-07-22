// PyBridgePlugin.h — Python 桥接宿主插件（M6）
// IBarPlugin 外壳 + IBarPluginHost：内嵌 CPython 解释器，
// 扫描 <exeDir>/plugins_py/ 并为每个 Python 子插件创建 PyProxyViewModel。
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
    void shutdown() override;        // 销毁全部子插件 VM；解释器保持存活（见 cpp 注释）

    // IBarPluginHost（两阶段：先枚举 id，核心按 config 过滤后再实例化）
    QStringList subPluginIds() override;
    xuanhub::SubPluginInfo createSubPlugin(const QString& id, const QVariantMap& cfg) override;

private:
    xuanhub::BarPluginContext m_ctx;   // 拷贝保存（init 传入的 ctx 是核心栈上对象）
    QHash<QString, QString> m_dirs;    // id → 子插件目录（subPluginIds 扫描结果）
    // 子插件 VM 由 pybackend.dll 工厂创建（本 DLL 不触碰 Python 类型，
    // 只持有 QObject*；拆分原因见 PyBackendApi.h 头注释）
    QList<QObject*> m_vms;             // 已实例化的子插件 VM，shutdown 统一销毁
};
