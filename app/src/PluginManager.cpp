// PluginManager.cpp
#include "PluginManager.h"
#include "ConfigService.h"
#include "PluginDependencyGraph.h"
#include "PluginManifest.h"
#include "PluginRuntime.h"
#include "PluginHostServices.h"
#include "PluginContextObject.h"
#include "SemanticVersion.h"
#include "barplugin/EventBus.h"
#include "barplugin/IBarPluginHost.h"
#include "barplugin/IBarPluginHostLifecycle.h"
#include "barplugin/IBarPluginHostConfigurable.h"
#include "barplugin/IBarPluginHostServices.h"
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

PluginManager::PluginManager(xuanhub::EventBus* bus, const QString& configDir,
                             HostEnvironment hostEnvironment,
                             QObject* parent)
    : QObject(parent), m_bus(bus), m_configDir(configDir),
      m_hostEnvironment(std::move(hostEnvironment)) {}

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
    if (!allowExisting) m_loadErrors.clear();
    for (const auto& entry : enabled) m_loadErrors.remove(entry.id);
    m_shut = false;   // 复位闩锁，支持 shutdownAll() → loadFrom() 热重载周期

    QHash<QString, const PluginConfigEntry*> enabledMap;
    for (const auto& e : enabled) enabledMap.insert(e.id, &e);

    QDir base(pluginsDir);
    if (!base.exists()) {
        qDebug() << "[Plugin] plugins dir not found:" << pluginsDir;
        for (const auto& entry : enabled)
            recordLoadError(entry.id, QStringLiteral("plugins directory does not exist"));
        return;
    }
    struct Candidate {
        QString dirPath;
        QString dllName;
        QString path;
        QString iid;
        PluginManifestResult manifest;
    };
    QVector<Candidate> candidates;
    QVector<xuanhub::PluginDescriptor> descriptors = m_descriptorById.values().toVector();
    const PluginValidationEnvironment validationEnvironment =
        PluginValidationEnvironment::current();
    const auto subdirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subdirs) {
        QDir d(base.filePath(sub));
        const auto dlls = d.entryList({QStringLiteral("*.dll")}, QDir::Files);
        for (const QString& dllName : dlls) {
            const QString path = d.absoluteFilePath(dllName);
            // sidecar 是发现阶段的信任边界：必须在构造 QPluginLoader 前完成
            // 解析和路径/ABI 校验，失败插件的 DLL 代码完全不进入宿主进程。
            PluginManifestResult manifest = PluginManifest::load(
                d.absolutePath(), dllName, validationEnvironment);
            if (!manifest.ok) {
                qWarning() << "[Plugin] manifest rejected:" << path << "->"
                           << manifest.error;
                recordLoadError(sub, manifest.error);
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
            // metaData() 读取 Qt 插件节，不调用 instance()，可安全参与预加载依赖规划。
            QPluginLoader probe(path);
            const QString iid = probe.metaData().value(QStringLiteral("IID")).toString();
            if (iid != QStringLiteral("xuanhub.BarPlugin/1.0")
                && iid != QStringLiteral("xuanhub.BarPlugin/2.0")) {
                qWarning() << "[Plugin] skip (IID mismatch):" << path << "->" << iid;
                recordLoadError(manifestId,
                    QStringLiteral("unsupported plugin IID: %1").arg(iid));
                continue;
            }
            const int iidApiVersion = iid.endsWith(QStringLiteral("/2.0")) ? 2 : 1;
            if (manifest.descriptor.apiVersion != iidApiVersion) {
                qWarning() << "[Plugin] manifest apiVersion/IID mismatch:" << path
                           << manifest.descriptor.apiVersion << iid;
                recordLoadError(manifestId,
                    QStringLiteral("manifest apiVersion does not match plugin IID"));
                continue;
            }
            descriptors.append(manifest.descriptor);
            candidates.append({d.absolutePath(), dllName, path, iid, std::move(manifest)});
        }
    }

    const PluginDependencyPlan dependencyPlan = PluginDependencyGraph::build(descriptors);
    for (auto it = dependencyPlan.errors.cbegin(); it != dependencyPlan.errors.cend(); ++it) {
        if (enabledMap.contains(it.key()) && !m_loadedIds.contains(it.key())) {
            qWarning() << "[Plugin] dependency precheck rejected:" << it.key() << "->"
                       << it.value();
            recordLoadError(it.key(), it.value());
        }
    }
    QHash<QString, int> candidateIndex;
    for (int i = 0; i < candidates.size(); ++i) {
        if (!candidateIndex.contains(candidates.at(i).manifest.descriptor.id))
            candidateIndex.insert(candidates.at(i).manifest.descriptor.id, i);
    }

    for (const QString& plannedId : dependencyPlan.loadOrder) {
        const auto candidateIt = candidateIndex.constFind(plannedId);
        if (candidateIt == candidateIndex.cend()) continue; // 已加载依赖无需重复执行。
        const Candidate& candidate = candidates.at(candidateIt.value());
        const QString path = candidate.path;
        const QString iid = candidate.iid;
        const PluginManifestResult& manifest = candidate.manifest;
        const QString manifestId = manifest.descriptor.id;
        const PluginConfigEntry* entry = enabledMap.value(manifestId, nullptr);
        QDir d(candidate.dirPath);

        bool dependencyUnavailable = false;
        for (const auto& dependency : manifest.descriptor.dependencies) {
            const auto loadedDependency = m_descriptorById.constFind(dependency.id);
            const bool available = loadedDependency != m_descriptorById.cend()
                && SemanticVersion::matches(loadedDependency->version,
                                            dependency.versionConstraint);
            if (!available && !dependency.optional) {
                qWarning() << "[Plugin] required dependency failed to load:"
                           << manifestId << "->" << dependency.id;
                recordLoadError(manifestId,
                    QStringLiteral("required dependency failed to load: %1")
                        .arg(dependency.id));
                dependencyUnavailable = true;
                break;
            }
        }
        if (dependencyUnavailable) continue;

            auto* loader = new QPluginLoader(path);
            // 清掉 Qt 默认的 PreventUnloadHint，使 unload() 真正卸载 DLL
            //（热重载/插件升级的前提；代价是必须严格先 shutdown 再 unload）
            loader->setLoadHints(QLibrary::ResolveAllSymbolsHint);

            // IID 校验
            const QString runtimeIid = loader->metaData().value(QStringLiteral("IID")).toString();
            if (runtimeIid != iid) {
                qWarning() << "[Plugin] IID changed after discovery:" << path
                           << iid << "->" << runtimeIid;
                recordLoadError(manifestId,
                    QStringLiteral("plugin IID changed during discovery"));
                delete loader;
                continue;
            }
            QObject* obj = loader->instance();
            if (!obj) {
                qWarning() << "[Plugin] instance failed:" << path << ":" << loader->errorString();
                recordLoadError(manifestId, loader->errorString());
                delete loader;
                continue;
            }
            QString runtimeError;
            std::unique_ptr<PluginRuntime> runtime =
                PluginRuntime::create(obj, iid, &runtimeError);
            if (!runtime) {
                qWarning() << "[Plugin] runtime adapter failed:" << path
                           << "->" << runtimeError;
                recordLoadError(manifestId, runtimeError);
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
                recordLoadError(manifestId, runtimeError);
                loader->unload();
                delete loader;
                continue;
            }
            // 按 id 去重：同名插件（多目录/多 DLL 副本）只加载第一份
            if (m_loadedIds.contains(descriptor.id)) {
                qWarning() << "[Plugin] duplicate id, skip:" << descriptor.id << "->" << path;
                recordLoadError(descriptor.id, QStringLiteral("duplicate plugin id"));
                loader->unload();
                delete loader;
                continue;
            }
            xuanhub::BarPluginContext ctx;
            ctx.eventBus = m_bus;
            ctx.configDir = m_configDir;
            ctx.pluginConfig = entry->config;
            auto hostServices = createHostServices(
                descriptor.id, xuanhub::HostPluginTrustLevel::TrustedInProcess,
                descriptor.permissions,
                xuanhub::grantedHostPermissions(
                    xuanhub::HostPluginTrustLevel::TrustedInProcess,
                    descriptor.permissions));
            ctx.hostServices = hostServices.get();
            for (const auto& dependency : descriptor.dependencies) {
                const auto loadedDependency = m_descriptorById.constFind(dependency.id);
                if (loadedDependency != m_descriptorById.cend()
                    && SemanticVersion::matches(loadedDependency->version,
                                                dependency.versionConstraint)) {
                    ctx.dependencyVersions.insert(dependency.id,
                                                  loadedDependency->version);
                }
            }
            auto eventScope = std::make_unique<xuanhub::PluginEventScope>(m_bus);
            ctx.eventScope = eventScope.get();
            QElapsedTimer initTimer;
            initTimer.start();
            const bool initialized = runtime->initialize(&ctx, &runtimeError);
            const qint64 initDurationMs = initTimer.elapsed();
            if (!initialized) {
                qWarning() << "[Plugin] init failed:" << descriptor.id
                           << "durationMs=" << initDurationMs << "->" << runtimeError;
                recordLoadError(descriptor.id,
                    runtimeError.isEmpty() ? QStringLiteral("plugin initialization failed")
                                           : runtimeError);
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
                m_descriptorById.insert(descriptor.id, descriptor);
                m_loadErrors.remove(descriptor.id);
                m_hosts.append(runtimePtr);
                retainHostServices(descriptor.id, std::move(hostServices));
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

            // V2 capability 决定是否创建 bar UI；无 barItem 的插件只保留后台
            // Runtime，不把 QML/ViewModel 暴露给布局模型。
            if (runtime->isV2()
                && !descriptor.capabilities.contains(QStringLiteral("barItem"))) {
                m_loaderById.insert(descriptor.id, loader);
                m_runtimeById.insert(descriptor.id, runtimePtr);
                m_scopeById.insert(descriptor.id, eventScope.get());
                m_runtimes.push_back(std::move(runtime));
                m_eventScopes.push_back(std::move(eventScope));
                m_loaders.append(loader);
                m_loadedIds.insert(descriptor.id);
                m_descriptorById.insert(descriptor.id, descriptor);
                m_loadErrors.remove(descriptor.id);
                m_services.append(runtimePtr);
                retainHostServices(descriptor.id, std::move(hostServices));
                qDebug() << "[Plugin] loaded background service:" << descriptor.id;
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
                recordLoadError(descriptor.id,
                    QStringLiteral("runtime qmlUrl differs from manifest"));
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
            lp.apiVersion = descriptor.apiVersion;
            lp.capabilities = descriptor.capabilities;
            lp.position = pos;
            lp.order = entry->order;
            lp.width = entry->width;
            lp.viewModel = runtime->viewModel();
            auto qmlContext = std::make_unique<PluginContextObject>(
                hostServices.get(), m_hostEnvironment.localeNotifier,
                m_hostEnvironment.themeNotifier);
            lp.pluginContext = qmlContext.get();
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
            m_descriptorById.insert(descriptor.id, descriptor);
            m_loadErrors.remove(descriptor.id);
            retainHostServices(descriptor.id, std::move(hostServices),
                               std::move(qmlContext));
            qDebug() << "[Plugin] loaded:" << descriptor.id
                     << "pos=" << lp.position << "qml=" << qmlUrl.toString();
    }
    for (const auto& entry : enabled) {
        if (!m_loadedIds.contains(entry.id) && !m_loadErrors.contains(entry.id)) {
            recordLoadError(entry.id,
                QStringLiteral("enabled plugin has no valid discovered runtime"));
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
        recordLoadError(entry.id, QStringLiteral("host failed to create sub-plugin"));
        return false;
    }
    LoadedPlugin lp;
    lp.id = entry.id;
    lp.position = sanitizePosition(entry.position, entry.id);
    lp.order = entry.order;
    lp.width = entry.width;
    lp.viewModel = sp.viewModel;
    auto hostServices = createHostServices(
        entry.id, xuanhub::HostPluginTrustLevel::Isolated,
        sp.permissions,
        xuanhub::grantedHostPermissions(xuanhub::HostPluginTrustLevel::Isolated,
                                        sp.permissions));
    auto* hostRuntime = m_runtimeById.value(hostId);
    auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
    if (auto* binder = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostServices*>(hostObject) : nullptr) {
        if (!binder->bindSubPluginHostServices(entry.id, hostServices.get())) {
            if (auto* lifecycle = qobject_cast<xuanhub::IBarPluginHostLifecycle*>(hostObject))
                lifecycle->destroySubPlugin(entry.id);
            recordLoadError(entry.id,
                            QStringLiteral("host failed to bind sub-plugin services"));
            return false;
        }
    }
    auto qmlContext = std::make_unique<PluginContextObject>(
        hostServices.get(), m_hostEnvironment.localeNotifier,
        m_hostEnvironment.themeNotifier);
    lp.pluginContext = qmlContext.get();
    lp.capabilities = sp.capabilities;
    lp.qmlUrl = QUrl::fromLocalFile(QDir(sp.dirPath).absoluteFilePath(sp.qmlFilePath));
    lp.health.state = xuanhub::PluginState::Running;
    lp.health.startedAt = QDateTime::currentDateTimeUtc();
    lp.health.lastHealthyAt = lp.health.startedAt;
    lp.health.loadDurationMs = createDurationMs;
    m_loaded.append(lp);
    m_loadedIds.insert(entry.id);
    xuanhub::PluginDescriptor descriptor;
    descriptor.id = sp.meta.id.isEmpty() ? entry.id : sp.meta.id;
    descriptor.displayName = sp.meta.displayName;
    descriptor.version = sp.meta.version;
    descriptor.author = sp.meta.author;
    m_descriptorById.insert(entry.id, descriptor);
    m_loadErrors.remove(entry.id);
    m_subPluginHostId.insert(entry.id, hostId);
    retainHostServices(entry.id, std::move(hostServices), std::move(qmlContext));
    qDebug() << "[Plugin] loaded:" << entry.id << "pos=" << lp.position
             << "qml=" << lp.qmlUrl.toLocalFile() << "(via host" << hostId << ")";
    return true;
}

bool PluginManager::loadOne(const QString& pluginsDir, const PluginConfigEntry& entry) {
    if (entry.id.isEmpty() || m_loadedIds.contains(entry.id)) return false;
    m_loadErrors.remove(entry.id);
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
        m_descriptorById.remove(id);
        removeHostServices(id);
        return true;
    }

    auto* runtime = m_runtimeById.value(id);
    auto* loader = m_loaderById.value(id);
    if (!runtime || !loader) {
        if (m_loadErrors.remove(id)) return true;
        return false;
    }

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
    m_services.removeAll(runtime);
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
    m_descriptorById.remove(id);
    removeRuntime(runtime);
    if (!loader->unload()) {
        qWarning() << "[Plugin] unload one failed:" << loader->fileName()
                   << loader->errorString();
    }
    delete loader;
    removeHostServices(id);
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
    for (auto* runtime : m_services) {
        const QString id = runtime->descriptor().id;
        QElapsedTimer timer;
        timer.start();
        runtime->shutdown();
        qDebug() << "[Plugin] shutdown background service:" << id
                 << "durationMs=" << timer.elapsed();
    }
    m_services.clear();
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
    m_descriptorById.clear();
    m_hostServiceScopes.clear();
}

std::unique_ptr<PluginHostServices> PluginManager::createHostServices(
    const QString& id, xuanhub::HostPluginTrustLevel trustLevel,
    const QStringList& declaredPermissions,
    const QStringList& grantedPermissions) const {
    PluginHostServices::Environment environment;
    environment.eventBus = m_bus;
    environment.configDir = m_configDir;
    environment.activeLocale = m_hostEnvironment.activeLocale;
    environment.currentPalette = m_hostEnvironment.currentPalette;
    return std::make_unique<PluginHostServices>(
        id, trustLevel, declaredPermissions, grantedPermissions,
        std::move(environment));
}

void PluginManager::retainHostServices(
    const QString& id, std::unique_ptr<PluginHostServices> services,
    std::unique_ptr<PluginContextObject> qmlContext) {
    m_hostServiceScopes.push_back(
        {id, std::move(services), std::move(qmlContext)});
}

void PluginManager::removeHostServices(const QString& id) {
    const auto it = std::find_if(m_hostServiceScopes.begin(), m_hostServiceScopes.end(),
        [&id](const HostServiceScope& scope) { return scope.id == id; });
    if (it != m_hostServiceScopes.end()) m_hostServiceScopes.erase(it);
}

void PluginManager::removeRuntime(PluginRuntime* runtime) {
    const auto it = std::find_if(m_runtimes.begin(), m_runtimes.end(),
        [runtime](const auto& owned) { return owned.get() == runtime; });
    if (it != m_runtimes.end()) m_runtimes.erase(it);
}

void PluginManager::recordLoadError(const QString& id, const QString& error) {
    if (id.isEmpty() || error.isEmpty()) return;
    m_loadErrors.insert(id, error);
}
