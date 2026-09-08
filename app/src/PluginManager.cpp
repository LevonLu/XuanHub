// PluginManager.cpp
#include "PluginManager.h"
#include "ConfigService.h"
#include "PluginDependencyGraph.h"
#include "PluginCatalog.h"
#include "PluginRuntime.h"
#include "PluginHostServices.h"
#include "PluginContextObject.h"
#include "SemanticVersion.h"
#include "barplugin/EventBus.h"
#include "barplugin/IBarPluginHost.h"
#include "barplugin/IBarPluginHostLifecycle.h"
#include "barplugin/IBarPluginHostConfigurable.h"
#include "barplugin/IBarPluginHostConfigMigration.h"
#include "barplugin/IBarPluginHostConfigValidation.h"
#include "barplugin/IBarPluginHostDiagnostics.h"
#include "barplugin/IBarPluginHostDependencies.h"
#include "barplugin/IBarPluginHostEventBus.h"
#include "barplugin/IBarPluginHostServices.h"
#include "barplugin/ManagedPluginStore.h"
#include "barplugin/PluginEventScope.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QLibrary>
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>
#include <QScopedValueRollback>

#include <algorithm>
#include <utility>

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

QString effectivePosition(QString configured,
                          const xuanhub::PluginLayoutPreference& preference,
                          const QString& id) {
    // Manifest region 只是默认偏好；用户在 config.json 中显式选择的位置始终优先。
    if (configured.isEmpty() && preference.declared && !preference.region.isEmpty())
        configured = preference.region;
    return sanitizePosition(configured, id);
}

QVariantMap actionResult(bool accepted, bool pending = false,
                         const QString& errorCode = {},
                         const QString& errorMessage = {}) {
    return {
        {QStringLiteral("accepted"), accepted},
        {QStringLiteral("pending"), pending},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("errorMessage"), errorMessage},
    };
}

QVariantMap actionState(QObject* viewModel, const QString& actionId) {
    if (!viewModel) return {};
    QVariantMap states = viewModel->property("actionStates").toMap();
    if (states.isEmpty()) {
        states = viewModel->property("values").toMap()
                     .value(QStringLiteral("actionStates")).toMap();
    }
    return states.value(actionId).toMap();
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
    Q_EMIT pluginsChanged();
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
    if (!allowExisting) {
        m_loadErrors.clear();
        m_migrationSuggestions.clear();
        m_candidateHostBySubPluginId.clear();
    }
    for (const auto& entry : enabled) {
        m_loadErrors.remove(entry.id);
        m_migrationSuggestions.remove(entry.id);
    }
    m_shut = false;   // 复位闩锁，支持 shutdownAll() → loadFrom() 热重载周期

    QHash<QString, const PluginConfigEntry*> enabledMap;
    for (const auto& e : enabled) enabledMap.insert(e.id, &e);

    QSet<QString> enabledIds;
    for (const auto& entry : enabled) enabledIds.insert(entry.id);
    auto catalog = PluginCatalog::scan(pluginsDir, m_configDir, enabledIds,
                                       m_loadedIds, m_unloadErrors);
    for (auto it = catalog.errors.cbegin(); it != catalog.errors.cend(); ++it)
        recordLoadError(it.key(), it.value());
    auto& candidates = catalog.candidates;
    auto descriptors = m_descriptorById.values().toVector();
    for (const auto& candidate : candidates) descriptors.append(candidate.manifest.descriptor);

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
        auto& candidate = candidates[candidateIt.value()];
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

            // 发现与实例化复用同一个 loader，避免同路径的第二个 Qt 库缓存引用
            // 在 unload 后继续持有 Windows 模块句柄。
            auto* loader = candidate.loader.release();

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

            if (qobject_cast<xuanhub::IBarPluginHost*>(obj)
                && !qobject_cast<xuanhub::IBarPluginHostLifecycle*>(obj)) {
                recordLoadError(manifestId, QStringLiteral(
                    "legacy host is unsupported: independent sub-plugin lifecycle is required"));
                runtime.reset();
                loader->unload();
                delete loader;
                continue;
            }

            QString runtimeId;
            QString runtimeVersion;
            {
                // 插件常用 QStringLiteral 返回描述字段，其字符缓冲位于插件 DLL。
                // 只在 DLL 仍加载时读取，并显式深拷贝日志字段；后续状态统一采用
                // 宿主解析 sidecar 得到的 descriptor，避免卸载后留下悬空字符串。
                const auto runtimeDescriptor = runtime->descriptor();
                runtimeId = QString(runtimeDescriptor.id.constData(),
                                    runtimeDescriptor.id.size());
                runtimeVersion = QString(runtimeDescriptor.version.constData(),
                                         runtimeDescriptor.version.size());
            }
            if (!runtime->descriptorMatches(manifest.descriptor, &runtimeError)) {
                qWarning() << "[Plugin] sidecar/runtime descriptor mismatch:" << path
                           << "manifest=" << manifestId << manifest.descriptor.version
                           << "runtime=" << runtimeId << runtimeVersion
                           << "->" << runtimeError;
                recordLoadError(manifestId, runtimeError);
                loader->unload();
                delete loader;
                continue;
            }
            auto descriptor = manifest.descriptor;
            // V1 运行时接口没有 actions 字段；声明式动作来自已在发现阶段完成
            // 信任边界校验的 sidecar。V2 两侧已做逐字段一致性校验，覆盖后值相同。
            descriptor.actions = manifest.descriptor.actions;
            descriptor.layout = manifest.descriptor.layout;
            // 按 id 去重：同名插件（多目录/多 DLL 副本）只加载第一份
            if (m_loadedIds.contains(descriptor.id)) {
                qWarning() << "[Plugin] duplicate id, skip:" << descriptor.id << "->" << path;
                recordLoadError(descriptor.id, QStringLiteral("duplicate plugin id"));
                loader->unload();
                delete loader;
                continue;
            }
            QVariantMap effectiveConfig = entry->config;
            const PluginRuntime::MigrationResult migration = runtime->migrateConfig(
                entry->configVersion, entry->config);
            if (migration.status == PluginRuntime::MigrationStatus::Migrated) {
                effectiveConfig = migration.config;
                m_migrationSuggestions.insert(manifestId, {
                    entry->configVersion, migration.targetVersion,
                    migration.config, {}});
                qDebug() << "[Plugin] config migration suggested:" << manifestId
                         << entry->configVersion << "->" << migration.targetVersion;
            } else if (migration.status == PluginRuntime::MigrationStatus::Unsupported
                       || migration.status == PluginRuntime::MigrationStatus::Rejected) {
                const QString migrationError = QStringLiteral(
                    "config migration %1 -> %2 failed: %3")
                    .arg(entry->configVersion).arg(migration.targetVersion)
                    .arg(migration.error);
                m_migrationSuggestions.insert(manifestId, {
                    entry->configVersion, migration.targetVersion,
                    {}, migrationError});
                qWarning() << "[Plugin]" << manifestId << migrationError;
                recordLoadError(manifestId, migrationError);
                loader->unload();
                delete loader;
                continue;
            }
            xuanhub::BarPluginContext ctx;
            ctx.eventBus = m_bus;
            ctx.configDir = m_configDir;
            ctx.pluginConfig = effectiveConfig;
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
            auto eventScope = std::make_unique<xuanhub::PluginEventScope>(
                m_bus, descriptor.id,
                xuanhub::grantedHostPermissions(
                    xuanhub::HostPluginTrustLevel::TrustedInProcess,
                    descriptor.permissions));
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
                eventScope->clear();
                runtime->shutdown();
                eventScope.reset();
                hostServices.reset();
                runtime.reset();
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
                m_configTracker.applied(*entry);
                Q_EMIT configStatesChanged();
                retainHostServices(descriptor.id, std::move(hostServices));
                qDebug() << "[Plugin] loaded host:" << descriptor.id;
                QObject::connect(
                    obj,
                    SIGNAL(subPluginDependencyInstallFinished(QString,bool,QString)),
                    this,
                    SLOT(onHostedDependencyInstallFinished(QString,bool,QString)),
                    Qt::UniqueConnection);
                const QStringList subIds = host->subPluginIds();
                if (subIds.isEmpty())
                    qDebug() << "[Plugin] host" << descriptor.id << "found no sub-plugins";
                for (const QString& sid : subIds) {
                    m_candidateHostBySubPluginId.insert(sid, descriptor.id);
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
                m_configTracker.applied(*entry);
                Q_EMIT configStatesChanged();
                retainHostServices(descriptor.id, std::move(hostServices));
                qDebug() << "[Plugin] loaded background service:" << descriptor.id;
                continue;
            }

            const QString pos = effectivePosition(entry->position, descriptor.layout,
                                                  descriptor.id);

            // qmlUrl：契约是相对 DLL 目录的文件名，拼成绝对 file URL；
            // 绝对 URL 原样采用——旧实现对 file:///C:/... 取 rel.path() 得到带
            // 前导斜杠的 "/C:/..."，再 fromLocalFile 会拼出损坏路径静默红占位
            const QUrl rel = QUrl::fromEncoded(runtime->qmlUrl().toEncoded());
            if (runtime->isV2()
                && (!rel.isRelative()
                    || QDir::cleanPath(rel.path())
                        != QDir::cleanPath(manifest.descriptor.entry.qml))) {
                qWarning() << "[Plugin] V2 qmlUrl differs from manifest:" << descriptor.id
                           << rel << manifest.descriptor.entry.qml;
                recordLoadError(descriptor.id,
                    QStringLiteral("runtime qmlUrl differs from manifest"));
                eventScope->clear();
                runtime->shutdown();
                eventScope.reset();
                hostServices.reset();
                runtime.reset();
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
            lp.actions = descriptor.actions;
            lp.layout = descriptor.layout;
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
            m_configTracker.applied(*entry);
            Q_EMIT configStatesChanged();
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

QVariantMap PluginManager::dependencyInstallPlan(const QString& pluginId) const {
    const QString hostId = m_candidateHostBySubPluginId.value(pluginId);
    auto* runtime = m_runtimeById.value(hostId);
    QObject* object = runtime ? runtime->instance() : nullptr;
    auto* dependencies = object
        ? qobject_cast<xuanhub::IBarPluginHostDependencies*>(object) : nullptr;
    return dependencies ? dependencies->subPluginDependencyPlan(pluginId) : QVariantMap{};
}

bool PluginManager::installPluginDependencies(const QString& pluginId) {
    const QString hostId = m_candidateHostBySubPluginId.value(pluginId);
    auto* runtime = m_runtimeById.value(hostId);
    QObject* object = runtime ? runtime->instance() : nullptr;
    auto* dependencies = object
        ? qobject_cast<xuanhub::IBarPluginHostDependencies*>(object) : nullptr;
    return dependencies && dependencies->installSubPluginDependencies(pluginId);
}

void PluginManager::onHostedDependencyInstallFinished(
    const QString& id, bool success, const QString& error) {
    if (success) m_loadErrors.remove(id);
    else recordLoadError(id, error.isEmpty()
        ? QStringLiteral("offline dependency installation failed") : error);
    Q_EMIT dependencyInstallFinished(id, success, error);
}

bool PluginManager::createHostedPlugin(const QString& hostId,
                                       xuanhub::IBarPluginHost* host,
                                       const PluginConfigEntry& entry) {
    if (!host || m_loadedIds.contains(entry.id)) return false;
    auto* hostRuntime = m_runtimeById.value(hostId);
    auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
    if (auto* migrationHost = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostConfigMigration*>(hostObject)
            : nullptr) {
        migrationHost->setSubPluginConfigVersion(entry.id, entry.configVersion);
    }
    QElapsedTimer createTimer;
    createTimer.start();
    const auto sp = host->createSubPlugin(entry.id, entry.config);
    const qint64 createDurationMs = createTimer.elapsed();
    if (!sp.viewModel) {
        qWarning() << "[Plugin] host createSubPlugin failed:" << entry.id
                   << "durationMs=" << createDurationMs;
        QString detail;
        if (auto* diagnostics = hostObject
                ? qobject_cast<xuanhub::IBarPluginHostDiagnostics*>(hostObject) : nullptr) {
            detail = diagnostics->subPluginLoadError(entry.id);
        }
        recordLoadError(entry.id, detail.isEmpty()
            ? QStringLiteral("host failed to create sub-plugin") : detail);
        return false;
    }
    if (sp.viewModel->metaObject()->indexOfSignal("configInitialized(QString,bool,QString)") < 0
        || sp.viewModel->metaObject()->indexOfSignal("configApplied(QString,QVariantMap,bool,QString)") < 0) {
        qobject_cast<xuanhub::IBarPluginHostLifecycle*>(hostObject)->destroySubPlugin(entry.id);
        recordLoadError(entry.id, QStringLiteral("legacy hosted plugin has no config acknowledgement contract"));
        return false;
    }
    QObject::connect(sp.viewModel, SIGNAL(configInitialized(QString,bool,QString)),
                     this, SLOT(onHostedConfigInitialized(QString,bool,QString)));
    QObject::connect(sp.viewModel, SIGNAL(configApplied(QString,QVariantMap,bool,QString)),
                     this, SLOT(onHostedConfigApplied(QString,QVariantMap,bool,QString)));
    m_configTracker.begin(entry);
    Q_EMIT configStatesChanged();
    QObject::connect(
        sp.viewModel,
        SIGNAL(configMigrationReady(QString,int,int,QVariantMap)),
        this,
        SLOT(onHostedMigrationReady(QString,int,int,QVariantMap)));
    QObject::connect(
        sp.viewModel,
        SIGNAL(configMigrationFailed(QString,int,int,QString)),
        this,
        SLOT(onHostedMigrationFailed(QString,int,int,QString)));
    LoadedPlugin lp;
    lp.id = entry.id;
    lp.position = effectivePosition(entry.position, sp.layout, entry.id);
    lp.order = entry.order;
    lp.width = entry.width;
    lp.viewModel = sp.viewModel;
    const QStringList grantedPermissions = xuanhub::grantedHostPermissions(
        xuanhub::HostPluginTrustLevel::Isolated, sp.permissions);
    auto hostServices = createHostServices(
        entry.id, xuanhub::HostPluginTrustLevel::Isolated,
        sp.permissions, grantedPermissions);
    auto eventScope = std::make_unique<xuanhub::PluginEventScope>(
        m_bus, entry.id, grantedPermissions);
    if (auto* eventBinder = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostEventBus*>(hostObject) : nullptr) {
        if (!eventBinder->bindSubPluginEventScope(entry.id, eventScope.get())) {
            if (auto* lifecycle = qobject_cast<xuanhub::IBarPluginHostLifecycle*>(hostObject))
                lifecycle->destroySubPlugin(entry.id);
            recordLoadError(entry.id,
                            QStringLiteral("host failed to bind sub-plugin event scope"));
            return false;
        }
    }
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
    lp.actions = sp.actions;
    lp.layout = sp.layout;
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
    descriptor.actions = sp.actions;
    descriptor.layout = sp.layout;
    m_descriptorById.insert(entry.id, descriptor);
    m_loadErrors.remove(entry.id);
    m_subPluginHostId.insert(entry.id, hostId);
    m_scopeById.insert(entry.id, eventScope.get());
    m_eventScopes.push_back(std::move(eventScope));
    retainHostServices(entry.id, std::move(hostServices), std::move(qmlContext));
    qDebug() << "[Plugin] loaded:" << entry.id << "pos=" << lp.position
             << "qml=" << lp.qmlUrl.toLocalFile() << "(via host" << hostId << ")";
    return true;
}

void PluginManager::onHostedMigrationReady(
    const QString& id, int currentVersion, int targetVersion,
    const QVariantMap& config) {
    m_migrationSuggestions.insert(id, {
        currentVersion, targetVersion, config, {}});
    m_loadErrors.remove(id);
    qDebug() << "[Plugin] hosted config migration suggested:" << id
             << currentVersion << "->" << targetVersion;
}

void PluginManager::onHostedMigrationFailed(
    const QString& id, int currentVersion, int targetVersion,
    const QString& error) {
    const QString migrationError = QStringLiteral(
        "config migration %1 -> %2 failed: %3")
        .arg(currentVersion).arg(targetVersion).arg(error);
    m_migrationSuggestions.insert(id, {
        currentVersion, targetVersion, {}, migrationError});
    recordLoadError(id, migrationError);
    qWarning() << "[Plugin]" << id << migrationError;
}

bool PluginManager::loadOne(const QString& pluginsDir, const PluginConfigEntry& entry) {
    if (entry.id.isEmpty() || m_loadedIds.contains(entry.id)) return false;
    m_loadErrors.remove(entry.id);
    m_shut = false;

    // 已加载 host 优先尝试创建子插件，不扫描或重启其他运行实例。
    for (auto it = m_hostById.cbegin(); it != m_hostById.cend(); ++it) {
        if (!it.value()->subPluginIds().contains(entry.id)) continue;
        const bool loaded = createHostedPlugin(it.key(), it.value(), entry);
        if (loaded) Q_EMIT pluginsChanged();
        return loaded;
    }

    loadFromImpl(pluginsDir, {entry}, true);
    const bool loaded = m_loadedIds.contains(entry.id);
    if (loaded) Q_EMIT pluginsChanged();
    return loaded;
}

const PluginManager::LoadedPlugin* PluginManager::loadedPlugin(const QString& id) const {
    for (const auto& plugin : m_loaded) {
        if (plugin.id == id) return &plugin;
    }
    return nullptr;
}

const PluginManager::ConfigMigrationSuggestion*
PluginManager::migrationSuggestion(const QString& id) const {
    const auto it = m_migrationSuggestions.constFind(id);
    return it == m_migrationSuggestions.cend() ? nullptr : &it.value();
}

QVariantMap PluginManager::invokeAction(const QString& pluginId,
                                        const QString& actionId) {
    const LoadedPlugin* plugin = loadedPlugin(pluginId);
    if (!plugin || !plugin->viewModel) {
        return actionResult(false, false, QStringLiteral("plugin_not_loaded"),
                            QStringLiteral("plugin is not loaded"));
    }
    const auto declared = std::find_if(
        plugin->actions.cbegin(), plugin->actions.cend(),
        [&actionId](const xuanhub::PluginAction& action) {
            return action.id == actionId;
        });
    if (declared == plugin->actions.cend()) {
        return actionResult(false, false, QStringLiteral("action_not_declared"),
                            QStringLiteral("action is not declared by the plugin"));
    }
    if (!actionState(plugin->viewModel, actionId)
             .value(QStringLiteral("enabled"), true).toBool()) {
        return actionResult(false, false, QStringLiteral("action_disabled"),
                            QStringLiteral("action is currently disabled"));
    }

    const bool hosted = m_subPluginHostId.contains(pluginId);
    const char* method = hosted ? "call" : "invokeAction";
    QVariant invocationResult;
    bool invoked = false;
    if (hosted) {
        invoked = QMetaObject::invokeMethod(
            plugin->viewModel, method, Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, invocationResult),
            Q_ARG(QString, actionId), Q_ARG(QVariantList, QVariantList{}));
    } else {
        invoked = QMetaObject::invokeMethod(
            plugin->viewModel, method, Qt::DirectConnection,
            Q_RETURN_ARG(QVariant, invocationResult), Q_ARG(QString, actionId));
    }
    if (!invoked) {
        return actionResult(false, false, QStringLiteral("action_unsupported"),
                            QStringLiteral("plugin does not implement the action entrypoint"));
    }

    const QVariantMap returned = invocationResult.toMap();
    if (!returned.isEmpty()) {
        const bool accepted = returned.value(
            QStringLiteral("accepted"),
            returned.value(QStringLiteral("queued"), true)).toBool();
        const bool pending = returned.value(
            QStringLiteral("pending"),
            hosted && returned.value(QStringLiteral("queued"), false).toBool()).toBool();
        if (!accepted) {
            return actionResult(false, false, QStringLiteral("action_rejected"),
                                returned.value(QStringLiteral("errorMessage")).toString());
        }
        QVariantMap result = returned;
        result.insert(QStringLiteral("accepted"), true);
        result.insert(QStringLiteral("pending"), pending);
        result.insert(QStringLiteral("errorCode"), QString());
        result.insert(QStringLiteral("errorMessage"), QString());
        return result;
    }
    if (invocationResult.metaType().id() == QMetaType::Bool
        && !invocationResult.toBool()) {
        return actionResult(false, false, QStringLiteral("action_rejected"),
                            QStringLiteral("plugin rejected the action"));
    }
    return actionResult(true, false);
}

QStringList PluginManager::dependentPluginIds(const QString& id) const {
    auto result = PluginDependencyGraph::stopOrder(
        m_descriptorById.values().toVector(), m_subPluginHostId, {id});
    result.removeAll(id);
    return result;
}

bool PluginManager::stopOne(const QString& id) {
    if (m_stopping) return false;
    QScopedValueRollback<bool> guard(m_stopping, true);
    const auto order = PluginDependencyGraph::stopOrder(
        m_descriptorById.values().toVector(), m_subPluginHostId, {id});
    // 宿主渲染层同步解除 QML 引用后才允许进入插件代码；关闭过程禁止重入。
    QStringList activeIds;
    for (const auto& target : order) {
        if (m_loadedIds.contains(target)) activeIds.append(target);
    }
    if (!activeIds.isEmpty()) Q_EMIT pluginsAboutToStop(activeIds);
    for (const auto& target : order) {
        if (!stopInstance(target)) return false;
    }
    return true;
}

bool PluginManager::stopInstance(const QString& id) {
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
        if (auto* scope = m_scopeById.value(id)) scope->clear();
        if (!lifecycle->destroySubPlugin(id)) {
            recordLoadError(id, QStringLiteral("host failed to destroy sub-plugin"));
            return false;
        }
        m_scopeById.remove(id);
        const auto scopeIt = std::find_if(m_eventScopes.begin(), m_eventScopes.end(),
            [&id](const auto& owned) { return owned->pluginId() == id; });
        if (scopeIt != m_eventScopes.end()) m_eventScopes.erase(scopeIt);
        for (int i = 0; i < m_loaded.size(); ++i) {
            if (m_loaded.at(i).id == id) {
                m_loaded.removeAt(i);
                break;
            }
        }
        m_subPluginHostId.remove(id);
        m_configTracker.removed(id);
        Q_EMIT configStatesChanged();
        m_loadedIds.remove(id);
        m_descriptorById.remove(id);
        m_migrationSuggestions.remove(id);
        removeHostServices(id);
        Q_EMIT pluginsChanged();
        return true;
    }

    auto* runtime = m_runtimeById.value(id);
    auto* loader = m_loaderById.value(id);
    if (!runtime || !loader) {
        if (m_unloadErrors.contains(id)) return false;
        m_loadErrors.remove(id);
        m_configTracker.removed(id);
        Q_EMIT configStatesChanged();
        return true; // 已停止/加载失败的目标也满足停止后置条件。
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
    m_configTracker.removed(id);
    Q_EMIT configStatesChanged();
    m_descriptorById.remove(id);
    m_migrationSuggestions.remove(id);
    // HostServices/QML context 可能保存指向插件元对象的 Qt 连接，必须在 DLL
    // 卸载前析构；否则稍后的 QObject 断连会读取已经释放的 staticMetaObject。
    removeHostServices(id);
    removeRuntime(runtime);
    if (m_deferUnloading) {
        m_deferredLoaders.append({id, loader});
        Q_EMIT pluginsChanged();
        return true;
    }
    const bool unloaded = unloadLibrary(id, loader);
    Q_EMIT pluginsChanged();
    return unloaded;
}

bool PluginManager::unloadLibrary(const QString& id, QPluginLoader* loader) {
    if (!loader->unload()) {
        const QString error = QStringLiteral("plugin DLL could not be unloaded; restart required: %1")
                                  .arg(loader->errorString());
        m_unloadErrors.insert(id, error);
        recordLoadError(id, error);
        // 保留 loader 到进程结束，禁止同一路径再次初始化已停止的实例。
        loader->setParent(this);
        return false;
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
    m_configTracker.layout(entry);
    for (auto& plugin : m_loaded) {
        if (plugin.id != entry.id) continue;
        plugin.position = effectivePosition(entry.position, plugin.layout, entry.id);
        plugin.order = entry.order;
        plugin.width = entry.width;
        return true;
    }
    return false;
}

PluginManager::ConfigValidationResult PluginManager::validateConfig(
    const QString& id, const QVariantMap& config) const {
    const QString hostId = m_subPluginHostId.value(id);
    if (!hostId.isEmpty()) {
        auto* hostRuntime = m_runtimeById.value(hostId);
        auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
        auto* validationHost = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostConfigValidation*>(hostObject)
            : nullptr;
        if (!validationHost) return {};
        // 嵌套事件循环继续处理 IPC/宿主服务，但屏蔽用户输入，避免保存过程重入。
        const auto result = validationHost->validateSubPluginConfig(id, config, 5000);
        return {result.supported, result.valid, result.error};
    }
    auto* runtime = m_runtimeById.value(id);
    if (!runtime) return {};
    const PluginRuntime::ValidationResult result = runtime->validateConfig(config);
    switch (result.status) {
    case PluginRuntime::ValidationStatus::Unsupported:
        return {};
    case PluginRuntime::ValidationStatus::Valid:
        return {true, true, {}};
    case PluginRuntime::ValidationStatus::Rejected:
        return {true, false, result.error};
    }
    return {};
}

PluginManager::ConfigApplyResult PluginManager::applyConfig(
    const PluginConfigEntry& entry) {
    if (const auto* pending = m_configTracker.pending(entry.id)) {
        if (pending->config == entry.config && pending->configVersion == entry.configVersion)
            return ConfigApplyResult::Pending;
        // 同一插件只允许一份配置等待确认，后续文件变化在回执后重试。
        return ConfigApplyResult::Pending;
    }
    const QString hostId = m_subPluginHostId.value(entry.id);
    if (!hostId.isEmpty()) {
        auto* hostRuntime = m_runtimeById.value(hostId);
        auto* hostObject = hostRuntime ? hostRuntime->instance() : nullptr;
        auto* configurable = hostObject
            ? qobject_cast<xuanhub::IBarPluginHostConfigurable*>(hostObject) : nullptr;
        const auto* plugin = loadedPlugin(entry.id);
        const bool hasReceipt = plugin && plugin->viewModel
            && plugin->viewModel->metaObject()->indexOfSignal(
                "configApplied(QString,QVariantMap,bool,QString)") >= 0;
        if (!configurable || !hasReceipt || !configurable->canApplySubPluginConfig(entry.id))
            return ConfigApplyResult::Unsupported;
        m_configTracker.begin(entry);
        Q_EMIT configStatesChanged();
        if (!configurable->applySubPluginConfig(entry.id, entry.config)) {
            m_configTracker.failed(entry.id, QStringLiteral("host rejected config request"));
            Q_EMIT configStatesChanged();
            return ConfigApplyResult::Rejected;
        }
        return ConfigApplyResult::Pending;
    }
    auto* runtime = m_runtimeById.value(entry.id);
    if (!runtime) return ConfigApplyResult::Unsupported;
    const PluginRuntime::ApplyResult result = runtime->applyConfig(entry.config);
    if (result.status == PluginRuntime::ApplyStatus::Unsupported
        || result.status == PluginRuntime::ApplyStatus::RequiresRestart)
        return ConfigApplyResult::Unsupported;
    if (result.status == PluginRuntime::ApplyStatus::Rejected) {
        m_configTracker.failed(entry.id, result.error);
        Q_EMIT configStatesChanged();
        return ConfigApplyResult::Rejected;
    }
    m_configTracker.applied(entry);
    Q_EMIT configStatesChanged();
    return ConfigApplyResult::Applied;
}

void PluginManager::onHostedConfigInitialized(const QString& id, bool success,
                                              const QString& error) {
    const auto* pending = m_configTracker.pending(id);
    if (!pending) return;
    const auto entry = *pending;
    if (success) m_configTracker.applied(entry);
    else m_configTracker.failed(id, error);
    Q_EMIT configStatesChanged();
}

void PluginManager::onHostedConfigApplied(const QString& id, const QVariantMap& config,
                                          bool success, const QString& error) {
    const auto* pending = m_configTracker.pending(id);
    if (!pending || pending->config != config) return;
    const auto entry = *pending;
    if (success) m_configTracker.applied(entry);
    else m_configTracker.failed(id, error);
    Q_EMIT configStatesChanged();
}

void PluginManager::shutdownAll() {
    if (m_shut) return;
    m_shut = true;
    for (auto& scope : m_eventScopes) scope->clear();
    auto order = PluginDependencyGraph::stopOrder(
        m_descriptorById.values().toVector(), m_subPluginHostId, m_loadedIds.values());
    // 保留全量退出的铁律：普通插件/子实例先停止，host 最后停止；所有 shutdown
    // 完成后再卸载 DLL。单实例停止仍复用同一 stopInstance 清理路径。
    std::stable_partition(order.begin(), order.end(),
        [this](const QString& id) { return !m_hostById.contains(id); });
    m_deferUnloading = true;
    for (const auto& id : order) {
        if (!stopOne(id))
            qWarning() << "[Plugin] shutdown failed; dependent module retained:" << id;
    }
    m_deferUnloading = false;
    for (const auto& deferred : std::as_const(m_deferredLoaders))
        unloadLibrary(deferred.first, deferred.second);
    m_deferredLoaders.clear();
    m_candidateHostBySubPluginId.clear();
    Q_EMIT pluginsChanged();
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
    m_loadErrors.insert(id, QString(error.constData(), error.size()));
    m_configTracker.failed(id, error);
    Q_EMIT configStatesChanged();
}
