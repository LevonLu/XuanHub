// PluginManager.cpp
#include "PluginManager.h"
#include "ConfigService.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>

PluginManager::PluginManager(xuanhub::EventBus* bus, const QString& configDir, QObject* parent)
    : QObject(parent), m_bus(bus), m_configDir(configDir) {}

PluginManager::~PluginManager() {
    shutdownAll();
    qDeleteAll(m_loaders);
}

void PluginManager::loadFrom(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled) {
    QHash<QString, const PluginConfigEntry*> enabledMap;
    for (const auto& e : enabled) enabledMap.insert(e.id, &e);

    QDir base(pluginsDir);
    if (!base.exists()) {
        qDebug() << "[Plugin] plugins dir not found:" << pluginsDir;
        return;
    }
    const auto subdirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subdirs) {
        QDir d(base.filePath(sub));
        const auto dlls = d.entryList({QStringLiteral("*.dll")}, QDir::Files);
        for (const QString& dllName : dlls) {
            const QString path = d.absoluteFilePath(dllName);
            auto* loader = new QPluginLoader(path);

            // IID 校验
            const QString iid = loader->metaData().value(QStringLiteral("IID")).toString();
            if (iid != QStringLiteral("xuanhub.BarPlugin/1.0")) {
                qWarning() << "[Plugin] skip (IID mismatch):" << path << "->" << iid;
                delete loader;
                continue;
            }
            QObject* obj = loader->instance();
            if (!obj) {
                qWarning() << "[Plugin] instance failed:" << path << ":" << loader->errorString();
                delete loader;
                continue;
            }
            auto* plugin = qobject_cast<xuanhub::IBarPlugin*>(obj);
            if (!plugin) {
                qWarning() << "[Plugin] qobject_cast<IBarPlugin*> failed:" << path;
                loader->unload();
                delete loader;
                continue;
            }

            const auto meta = plugin->metadata();
            const PluginConfigEntry* entry = enabledMap.value(meta.id, nullptr);
            if (!entry) {
                qDebug() << "[Plugin] not enabled in config, skip:" << meta.id;
                loader->unload();
                delete loader;
                continue;
            }

            xuanhub::BarPluginContext ctx;
            ctx.eventBus = m_bus;
            ctx.configDir = m_configDir;
            ctx.pluginConfig = entry->config;
            if (!plugin->init(&ctx)) {
                qWarning() << "[Plugin] init failed:" << meta.id;
                loader->unload();
                delete loader;
                continue;
            }

            // qmlUrl：插件返回相对 DLL 目录的文件名，拼成绝对 file URL
            const QUrl rel = plugin->qmlUrl();
            const QString qmlPath = rel.isRelative()
                ? d.absoluteFilePath(rel.path())
                : rel.path();
            LoadedPlugin lp;
            lp.id = meta.id;
            lp.position = entry->position.isEmpty() ? QStringLiteral("right") : entry->position;
            lp.order = entry->order;
            lp.viewModel = plugin->viewModel();
            lp.qmlUrl = QUrl::fromLocalFile(qmlPath);
            lp.iface = plugin;

            m_loaders.append(loader);
            m_loaded.append(lp);
            qDebug() << "[Plugin] loaded:" << meta.id
                     << "pos=" << lp.position << "qml=" << qmlPath;
        }
    }
}

void PluginManager::shutdownAll() {
    if (m_shut) return;
    m_shut = true;
    for (const auto& lp : m_loaded) {
        if (lp.iface) lp.iface->shutdown();
    }
    for (auto* loader : m_loaders) loader->unload();
}
