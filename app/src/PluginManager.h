// PluginManager.h — 插件扫描/加载/生命周期
// 扫描 plugins/<id>/ 下的 *.dll，校验 IID 后 qobject_cast<IBarPlugin*>，
// 按 config 启用列表 init，收集 viewModel + qmlUrl 供渲染层使用。
#pragma once
#include <QObject>
#include <QVector>
#include <QUrl>
#include <QPluginLoader>

#include "barplugin/IBarPlugin.h"

struct PluginConfigEntry;
namespace xuanhub { class EventBus; }

class PluginManager : public QObject {
    Q_OBJECT
public:
    struct LoadedPlugin {
        QString id;
        QString position;            // left/center/right
        int order = 0;
        QObject* viewModel = nullptr;
        QUrl qmlUrl;                 // 绝对 file URL
        xuanhub::IBarPlugin* iface = nullptr;
    };

    PluginManager(xuanhub::EventBus* bus, const QString& configDir, QObject* parent = nullptr);
    ~PluginManager() override;

    void loadFrom(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled);

    const QVector<LoadedPlugin>& loaded() const { return m_loaded; }
    void shutdownAll();

private:
    xuanhub::EventBus* m_bus;
    QString m_configDir;
    QVector<LoadedPlugin> m_loaded;
    QVector<QPluginLoader*> m_loaders;
    bool m_shut = false;
};
