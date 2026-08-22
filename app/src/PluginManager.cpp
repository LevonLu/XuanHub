// PluginManager.cpp
#include "PluginManager.h"
#include "ConfigService.h"
#include "PluginManifest.h"
#include "PluginRuntime.h"
#include "barplugin/EventBus.h"
#include "barplugin/IBarPluginHost.h"
#include "barplugin/IBarPluginHostLifecycle.h"
#include "barplugin/IBarPluginHostConfigurable.h"
#include "barplugin/PluginEventScope.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>

namespace {
// position 校验：空默认 right；非法值告警并回落 right
//（否则三个 FilterModel 都不接受，插件加载成功却静默不显示）
QString sanitizePosition(QString pos, const QString& id) {
    if (pos.isEmpty()) return QStringLiteral("right");
    if (pos != QStringLiteral("left") && pos != QStringLiteral("center")
        && pos != QStringLiteral("right")) {
        qWarning() << "[Plugin] invalid position" << pos << "for" << id << "-> right";
        return QStringLiteral("right");
    }
    return pos;
}
} // namespace

PluginManager::PluginManager(xuanhub::EventBus* bus, const QString& configDir, QObject* parent)
    : QObject(parent), m_bus(bus), m_configDir(configDir) {}

PluginManager::~PluginManager() {
    shutdownAll();
}

void PluginManager::loadFrom(const QString& pluginsDir, const QList<PluginConfigEntry>& enabled) {
    loadFromImpl(pluginsDir, enabled, false);
}

void PluginManager::loadFromImpl(const QString& pluginsDir,
                                 const QList<PluginConfigEntry>& enabled,
                                 bool allowExisting) {
    // 重入保护：必须先 shutdownAll() 再 loadFrom()——直接重入会把新 id 追加进
    // m_loaded（旧 id 靠 m_loadedIds 挡住刷 duplicate 告警），模型与 manager 状态分裂
    if (!allowExisting && (!m_loaded.isEmpty() || !m_loaders.isEmpty())) {
        qWarning() << "[Plugin] loadFrom called without prior shutdownAll, ignored";
        return;
    }
    m_shut = false;   // 复位闩锁，支持 shutdownAll() → loadFrom() 热重载周期

    QHash<QString, const PluginConfigEntry*> enabledMap;
    for (const auto& e : enabled) enabledMap.insert(e.id, &e);

    QDir base(pluginsDir);
    if (!base.exists()) {
        qDebug() << "[Plugin] plugins dir not found:" << pluginsDir;
        return;
    }
    const auto subdirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const PluginValidationEnvironment validationEnvironment =
        PluginValidationEnvironment::current();
    for (const QString& sub : subdirs) {
        QDir d(base.filePath(sub));
        const auto dlls = d.entryList({QStringLiteral("*.dll")}, QDir::Files);
        for (const QString& dllName : dlls) {
            const QString path = d.absoluteFilePath(dllName);
            // sidecar 是发现阶段的信任边界：必须在构造 QPluginLoader 前完成
            // 解析和路径/ABI 校验，失败插件的 DLL 代码完全不进入宿主进程。
            const PluginManifestResult manifest = PluginManifest::load(
                d.absolutePath(), dllName, validationEnvironment);
            if (!manifest.ok) {
                qWarning() << "[Plugin] manifest rejected:" << path << "->"
                           << manifest.error;
                continue;
            }
            const QString manifestId = manifest.descriptor.id;
            if (m_loadedIds.contains(manifestId)) {
                qWarning() << "[Plugin] duplicate manifest id, skip:"
                           << manifestId << "->" << path;
                continue;
            }
            const PluginConfigEntry* entry = enabledMap.value(manifestId, nullptr);
            if (!entry) {
                qDebug() << "[Plugin] not enabled in config, skip:" << manifestId;
                continue;
            }
            auto* loader = new QPluginLoader(path);
            // 清掉 Qt 默认的 PreventUnloadHint，使 unload() 真正卸载 DLL
            //（热重载/插件升级的前提；代价是必须严格先 shutdown 再 unload）
            loader->setLoadHints(QLibrary::ResolveAllSymbolsHint);

            // IID 校验
            const QString iid = loader->metaData().value(QStringLiteral("IID")).toString();
            if (iid != QStringLiteral("xuanhub.BarPlugin/1.0")
                && iid != QStringLiteral("xuanhub.BarPlugin/2.0")) {
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
            QString runtimeError;
            std::unique_ptr<PluginRuntime> runtime =
                PluginRuntime::create(obj, iid, &runtimeError);
            if (!runtime) {
                qWarning() << "[Plugin] runtime adapter failed:" << path
                           << "->" << runtimeError;
                loader->unload();
                delete loader;
                continue;
            }

            const auto descriptor = runtime->descriptor();
            if (!runtime->descriptorMatches(manifest.descriptor, &runtimeError)) {
                qWarning() << "[Plugin] sidecar/runtime descriptor mismatch:" << path
                           << "manifest=" << manifestId << manifest.descriptor.version
                           << "runtime=" << descriptor.id << descriptor.version
                           << "->" << runtimeError;
                loader->unload();
                delete loader;
                continue;
            }
            // 按 id 去重：同名插件（多目录/多 DLL 副本）只加载第一份
            if (m_loadedIds.contains(descriptor.id)) {
                qWarning() << "[Plugin] duplicate id, skip:" << descriptor.id << "->" << path;
                loader->unload();
                delete loader;
                continue;
            }
            xuanhub::BarPluginContext ctx;
            ctx.eventBus = m_bus;
            ctx.configDir = m_configDir;
            ctx.pluginConfig = entry->config;
            auto eventScope = std::make_unique<xuanhub::PluginEventScope>(m_bus);
            ctx.eventScope = eventScope.get();
            QElapsedTimer initTimer;
            initTimer.start();
            const bool initialized = runtime->initialize(&ctx, &runtimeError);
            const qint64 initDurationMs = initTimer.elapsed();
            if (!initialized) {
                qWarning() << "[Plugin] init failed:" << descriptor.id
                           << "durationMs=" << initDurationMs << "->" << runtimeError;
                loader->unload();
                delete loader;
                continue;
            }
            qDebug() << "[Plugin] initialized:" << descriptor.id
                     << "durationMs=" << initDurationMs;

            PluginRuntime* runtimePtr = runtime.get();

            // host 插件（如 pybridge）：自身不进可见列表；
            // 先枚举子插件 id，核心按 config 过滤/去重后再让 host 实例化
            if (auto* host = qobject_cast<xuanhub::IBarPluginHost*>(obj)) {
                m_loaderById.insert(descriptor.id, loader);
                m_runtimeById.insert(descriptor.id, runtimePtr);
                m_hostById.insert(descriptor.id, host);
                m_scopeById.insert(descriptor.id, eventScope.get());
                m_runtimes.push_back(std::move(runtime));
                m_eventScopes.push_back(std::move(eventScope));
                m_loaders.append(loader);
                m_loadedIds.insert(descriptor.id);
                m_hosts.append(runtimePtr);
                qDebug() << "[Plugin] loaded host:" << descriptor.id;
                const QStringList subIds = host->subPluginIds();
                if (subIds.isEmpty())
                    qDebug() << "[Plugin] host" << descriptor.id << "found no sub-plugins";
                for (const QString& sid : subIds) {
                    if (m_loadedIds.contains(sid)) {
                        qWarning() << "[Plugin] duplicate id (sub-plugin), skip:" << sid;
                        continue;
                    }
                    const PluginConfigEntry* se = enabledMap.value(sid, nullptr);
                    if (!se) {
                        qDebug() << "[Plugin] sub-plugin not enabled in config, skip:" << sid;
                        continue;
                    }
                    createHostedPlugin(descriptor.id, host, *se);
                }
                continue;
            }

            const QString pos = sanitizePosition(entry->position, descriptor.id);

            // qmlUrl：契约是相对 DLL 目录的文件名，拼成绝对 file URL；
            // 绝对 URL 原样采用——旧实现对 file:///C:/... 取 rel.path() 得到带
            // 前导斜杠的 "/C:/..."，再 fromLocalFile 会拼出损坏路径静默红占位
            const QUrl rel = runtime->qmlUrl();
            if (runtime->isV2()
                && (!rel.isRelative()
                    || QDir::cleanPath(rel.path())
                        != QDir::cleanPath(manifest.descriptor.entry.qml))) {
                qWarning() << "[Plugin] V2 qmlUrl differs from manifest:" << descriptor.id
                           << rel << manifest.descriptor.entry.qml;
                runtime->shutdown();
                loader->unload();
                delete loader;
                continue;
            }
            const QUrl qmlUrl = rel.isRelative()
                ? QUrl::fromLocalFile(d.absoluteFilePath(rel.path()))
                : rel;
            LoadedPlugin lp;
            lp.id = descriptor.id;
            lp.position = pos;
            lp.order = entry->order;
            lp.width = entry->width;
            lp.viewModel = runtime->viewModel();
            lp.qmlUrl = qmlUrl;
            lp.health.state = xuanhub::PluginState::Running;
            lp.health.startedAt = QDateTime::currentDateTimeUtc();
            lp.health.lastHealthyAt = lp.health.startedAt;
            lp.health.loadDurationMs = initDurationMs;

            m_eventScopes.push_back(std::move(eventScope));
            m_runtimes.push_back(std::move(runtime));
            m_loaders.append(loader);
            m_loaderById.insert(descriptor.id, loader);
            m_runtimeById.insert(descriptor.id, runtimePtr);
            m_scopeById.insert(descriptor.id, m_eventScopes.back().get());
            m_loaded.append(lp);
            m_loadedIds.insert(descriptor.id);
            qDebug() << "[Plugin] loaded:" << descriptor.id
                     << "pos=" << lp.position << "qml=" << qmlUrl.toString();
        }
    }
}

bool PluginManager::createHostedPlugin(const QString& hostId,
                                       xuanhub::IBarPluginHost* host,
                                       const PluginConfigEntry& entry) {
    if (!host || m_loadedIds.contains(entry.id)) return false;
    QElapsedTimer createTimer;
    createTimer.start();
    const auto sp = host->createSubPlugin(entry.id, entry.config);
    const qint64 createDurationMs = createTimer.elapsed();
    if (!sp.viewModel) {
        qWarning() << "[Plugin] host createSubPlugin failed:" << entry.id
                   << "durationMs=" << createDurationMs;
        return false;
    }
    LoadedPlugin lp;
    lp.id = entry.id;
    lp.position = sanitizePosition(entry.position, entry.id);
    lp.order = entry.order;
    lp.width = entry.width;
    lp.viewModel = sp.viewModel;
    lp.qmlUrl = QUrl::fromLocalFile(QDir(sp.dirPath).absoluteFilePath(sp.qmlFilePath));
    lp.health.state = xuanhub::PluginState::Running;
    lp.health.startedAt = QDateTime::currentDateTimeUtc();
    lp.health.lastHealthyAt = lp.health.startedAt;
    lp.health.loadDurationMs = createDurationMs;
    m_loaded.append(lp);
    m_loadedIds.insert(entry.id);
    m_subPluginHostId.insert(entry.id, hostId);
    qDebug() << "[Plugin] loaded:" << entry.id << "pos=" << lp.position
             << "qml=" << lp.qmlUrl.toLocalFile() << "(via host" << hostId << ")";
    return true;
}

bool PluginManager::loadOne(const QString& pluginsDir, const PluginConfigEntry& entry) {
    if (entry.id.isEmpty() || m_loadedIds.contains(entry.id)) return false;
    m_shut = false;

    // 已加载 host 优先尝试创建子插件，不扫描或重启其他运行实例。
    for (auto it = m_hostById.cbegin(); it != m_hostById.cend(); ++it) {
        if (!it.value()->subPluginIds().contains(entry.id)) continue;
        return createHostedPlugin(it.key(), it.value(), entry);
    }

    loadFromImpl(pluginsDir, {entry}, true);
    return m_loadedIds.contains(entry.id);
}

const PluginManager::LoadedPlugin* PluginManager::loadedPlugin(const QString& id) const {
    for (const auto& plugin : m_loaded) {
        if (plugin.id == id) return &plugin;
    }
    return nullptr;
}

QStringList PluginManager::dependentPluginIds(const QString& id) const {
    QStringList result;
    for (auto it = m_subPluginHostId.cbegin(); it != m_subPluginHostId.cend(); ++it) {
        if (it.value() == id) result.append(it.key());
    }
    result.sort();
    return result;
}

bool PluginManager::stopOne(const QString& id) {
    const QString hostId = m_subPluginHostId.value(id);
    if (!hostId.isEmpty()) {
        auto* hostRuntime = m_runtimeById.value(hostId);
        auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
        auto* lifecycle = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostLifecycle*>(hostObject) : nullptr;
        if (!lifecycle) {
            qWarning() << "[Plugin] host cannot stop sub-plugin independently:" << hostId << id;
            return false;
        }
        if (!lifecycle->destroySubPlugin(id)) return false;
        for (int i = 0; i < m_loaded.size(); ++i) {
            if (m_loaded.at(i).id == id) {
                m_loaded.removeAt(i);
                break;
            }
        }
        m_subPluginHostId.remove(id);
        m_loadedIds.remove(id);
        return true;
    }

    auto* runtime = m_runtimeById.value(id);
    auto* loader = m_loaderById.value(id);
    if (!runtime || !loader) return false;

    // host 自身停止前先销毁全部子实例；调用方必须先从 QML 模型移除这些 id。
    for (const QString& childId : dependentPluginIds(id)) {
        if (!stopOne(childId)) return false;
    }
    if (auto* scope = m_scopeById.take(id)) {
        scope->clear();
        const auto scopeIt = std::find_if(m_eventScopes.begin(), m_eventScopes.end(),
            [scope](const auto& owned) { return owned.get() == scope; });
        if (scopeIt != m_eventScopes.end()) m_eventScopes.erase(scopeIt);
    }

    QElapsedTimer timer;
    timer.start();
    runtime->shutdown();
    qDebug() << "[Plugin] shutdown one:" << id << "durationMs=" << timer.elapsed();

    m_hosts.removeAll(runtime);
    m_hostById.remove(id);
    m_runtimeById.remove(id);
    m_loaderById.remove(id);
    m_loaders.removeAll(loader);
    for (int i = 0; i < m_loaded.size(); ++i) {
        if (m_loaded.at(i).id == id) {
            m_loaded.removeAt(i);
            break;
        }
    }
    m_loadedIds.remove(id);
    removeRuntime(runtime);
    if (!loader->unload()) {
        qWarning() << "[Plugin] unload one failed:" << loader->fileName()
                   << loader->errorString();
    }
    delete loader;
    return true;
}

bool PluginManager::restartOne(const QString& pluginsDir,
                               const PluginConfigEntry& entry) {
    if (!stopOne(entry.id)) return false;
    return loadOne(pluginsDir, entry);
}

bool PluginManager::updateLayout(const PluginConfigEntry& entry) {
    for (auto& plugin : m_loaded) {
        if (plugin.id != entry.id) continue;
        plugin.position = sanitizePosition(entry.position, entry.id);
        plugin.order = entry.order;
        plugin.width = entry.width;
        return true;
    }
    return false;
}

PluginManager::ConfigApplyResult PluginManager::applyConfig(
    const PluginConfigEntry& entry) {
    const QString hostId = m_subPluginHostId.value(entry.id);
    if (!hostId.isEmpty()) {
        auto* hostRuntime = m_runtimeById.value(hostId);
        auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
        auto* configurable = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostConfigurable*>(hostObject) : nullptr;
        if (!configurable || !configurable->canApplySubPluginConfig(entry.id))
            return ConfigApplyResult::Unsupported;
        if (!configurable->applySubPluginConfig(entry.id, entry.config)) {
            qWarning() << "[Plugin] host applySubPluginConfig failed:"
                       << hostId << entry.id;
            return ConfigApplyResult::Rejected;
        }
        qDebug() << "[Plugin] hosted config applied in place:" << entry.id;
        return ConfigApplyResult::Applied;
    }
    auto* runtime = m_runtimeById.value(entry.id);
    if (!runtime) return ConfigApplyResult::Unsupported;
    const PluginRuntime::ApplyResult result = runtime->applyConfig(entry.config);
    if (result.status == PluginRuntime::ApplyStatus::Unsupported
        || result.status == PluginRuntime::ApplyStatus::RequiresRestart) {
        return ConfigApplyResult::Unsupported;
    }
    if (result.status == PluginRuntime::ApplyStatus::Rejected) {
        qWarning() << "[Plugin] config rejected:" << entry.id << result.error;
        return ConfigApplyResult::Rejected;
    }
    qDebug() << "[Plugin] config applied in place:" << entry.id;
    return ConfigApplyResult::Applied;
}

void PluginManager::shutdownAll() {
    if (m_shut) return;
    m_shut = true;
    // 先关闭所有作用域：从此拒绝插件新增订阅，并在任何 DLL shutdown/unload
    // 之前移除跨模块回调，即使插件遗留了仍存活的 QObject 也不会再被投递。
    for (auto& scope : m_eventScopes) scope->clear();
    // 顺序铁律：先让各插件 shutdown（删光各自 DLL 出身的 QObject），
    // 再 unload —— 根实例由 unload() 删除，随后 FreeLibrary
    for (const auto& lp : m_loaded) {
        auto* runtime = m_runtimeById.value(lp.id);
        if (!runtime) continue;
        QElapsedTimer timer;
        timer.start();
        runtime->shutdown();
        qDebug() << "[Plugin] shutdown:" << lp.id
                 << "durationMs=" << timer.elapsed();
    }
    // host 最后 shutdown：其子插件 VM 依赖 host 持有的运行时（如 CPython 解释器）
    for (auto* runtime : m_hosts) {
        const QString id = runtime->descriptor().id;
        QElapsedTimer timer;
        timer.start();
        runtime->shutdown();
        qDebug() << "[Plugin] shutdown host:" << id
                 << "durationMs=" << timer.elapsed();
    }
    m_hosts.clear();
    // 适配器只保存接口指针，仍应在 QPluginLoader 删除根 QObject 前释放，
    // 防止以后给适配器增加析构逻辑时误触已卸载代码。
    m_runtimeById.clear();
    m_runtimes.clear();
    for (auto* loader : m_loaders) {
        // unload 失败（如插件残留存活对象）时 DLL 实际未卸载——框架已清空
        // m_loaded 认为卸了，下次 loadFrom 会加载第二份，必须告警
        if (!loader->unload())
            qWarning() << "[Plugin] unload failed:" << loader->fileName()
                       << loader->errorString();
        delete loader;
    }
    m_loaders.clear();
    m_eventScopes.clear();
    m_loaderById.clear();
    m_hostById.clear();
    m_subPluginHostId.clear();
    m_scopeById.clear();
    m_loaded.clear();
    m_loadedIds.clear();
}

void PluginManager::removeRuntime(PluginRuntime* runtime) {
    const auto it = std::find_if(m_runtimes.begin(), m_runtimes.end(),
        [runtime](const auto& owned) { return owned.get() == runtime; });
    if (it != m_runtimes.end()) m_runtimes.erase(it);
}
