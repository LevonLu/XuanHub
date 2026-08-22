// SettingsViewModel.cpp
#include "SettingsViewModel.h"
#include "AutoStart.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "PluginManager.h"
#include "PluginPackageStore.h"
#include "PluginPackageValidator.h"
#include "PluginUpdateIndex.h"
#include "PluginUpdateService.h"
#include "SettingsPluginModel.h"
#include "SettingsPluginProxy.h"
#include "SettingsPluginSortModel.h"
#include "SecretStore.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "LocaleManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <utility>

namespace {
QVariantMap packageStoreResult(const PluginPackageStoreResult& result) {
    return {
        {QStringLiteral("ok"), result.ok},
        {QStringLiteral("rolledBack"), result.rolledBack},
        {QStringLiteral("cleanupPending"), result.cleanupPending},
        {QStringLiteral("error"), result.error},
        {QStringLiteral("id"), result.id},
        {QStringLiteral("version"), result.version},
        {QStringLiteral("previousVersion"), result.previousVersion},
        {QStringLiteral("payloadDirectory"), result.payloadDirectory},
    };
}
} // namespace

SettingsViewModel::SettingsViewModel(ConfigService* config, ThemeRegistry* registry,
                                     ThemeManager* themeMgr, LocaleManager* locale,
                                     QString exeDir,
                                     PluginListModel* barPlugins,
                                     PluginManager* pluginManager, QObject* parent)
    : QObject(parent), m_config(config), m_registry(registry), m_themeMgr(themeMgr),
      m_locale(locale), m_exeDir(std::move(exeDir)), m_barPlugins(barPlugins),
      m_pluginManager(pluginManager),
      m_updateService(new PluginUpdateService(
          QFileInfo(config->activePath()).absolutePath(), this)),
      m_pluginModel(new SettingsPluginModel(this)),
      m_sortModel(new SettingsPluginSortModel(this)),
      m_pluginProxy(new SettingsPluginProxy(m_pluginModel, this)) {
    m_sortModel->setSourceModel(m_pluginModel);
    m_sortModel->sort(0);   // sortColumn 置 0 启用排序；dynamicSortFilter 后续自动维持
    if (m_locale) {
        connect(m_locale, &LocaleManager::localeChanged, this, [this] {
            m_pluginModel->retranslate();
            setErrorMessage({});
        });
    }
    if (m_barPlugins) {
        connect(m_barPlugins, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last) {
            for (int row = first.row(); row <= last.row(); ++row) {
                const QString id = m_barPlugins->data(
                    m_barPlugins->index(row), PluginListModel::IdRole).toString();
                if (!id.isEmpty()) emit pluginHealthChanged(id);
            }
        });
        connect(m_barPlugins, &QAbstractItemModel::modelReset, this, [this] {
            emit pluginHealthChanged({});
        });
        connect(m_barPlugins, &PluginListModel::loadErrorChanged,
                this, &SettingsViewModel::pluginHealthChanged);
    }
    if (m_pluginManager) {
        connect(m_pluginManager, &PluginManager::dependencyInstallFinished, this,
                [this](const QString& id, bool success, const QString& error) {
            bool loaded = success;
            QString finalError = error;
            if (success) {
                loaded = false;
                for (const PluginConfigEntry& entry : m_config->pluginEntries()) {
                    if (entry.id != id) continue;
                    loaded = m_pluginManager->loadOne(
                        QDir(m_exeDir).filePath(QStringLiteral("plugins")), entry);
                    break;
                }
                if (!loaded) finalError = m_pluginManager->loadError(id);
            }
            if (m_barPlugins) {
                m_barPlugins->setPlugins(m_pluginManager->loaded());
                m_barPlugins->setLoadErrors(m_pluginManager->loadErrors());
            }
            if (!loaded) setErrorMessage(finalError.isEmpty()
                ? QCoreApplication::translate("xuanhub.settings.errors",
                                              "插件依赖安装后仍无法启动")
                : finalError);
            else setErrorMessage({});
            emit pluginHealthChanged(id);
            emit pluginDependencyInstallFinished(id, loaded, finalError);
        });
    }
    connect(m_updateService, &PluginUpdateService::checkingChanged,
            this, &SettingsViewModel::pluginUpdateCheckingChanged);
    connect(m_updateService, &PluginUpdateService::downloadingChanged,
            this, &SettingsViewModel::pluginUpdateDownloadingChanged);
    connect(m_updateService, &PluginUpdateService::checkFinished, this,
            [this](const QVariantList& updates, const QStringList& errors) {
        m_availablePluginUpdates = updates;
        m_pluginUpdateErrors = errors;
        emit availablePluginUpdatesChanged();
        emit pluginUpdateErrorsChanged();
        setErrorMessage({});
        emit pluginUpdateCheckFinished();
    });
    connect(m_updateService, &PluginUpdateService::downloadFinished, this,
            [this](const QString& path, const QVariantMap& update,
                   const QString& error) {
        if (!error.isEmpty()) {
            setErrorMessage(error);
            return;
        }
        QVariantMap package = inspectPluginPackage(path);
        if (!package.value(QStringLiteral("ok")).toBool()
            || package.value(QStringLiteral("id")) != update.value(QStringLiteral("id"))
            || package.value(QStringLiteral("version")) != update.value(QStringLiteral("version"))
            || package.value(QStringLiteral("runtime")) != update.value(QStringLiteral("runtime"))
            || package.value(QStringLiteral("packageHash"))
                != update.value(QStringLiteral("packageSha256"))
            || (!update.value(QStringLiteral("signerKeyId")).toString().isEmpty()
                && package.value(QStringLiteral("signerKeyId"))
                    != update.value(QStringLiteral("signerKeyId")))) {
            setErrorMessage(QCoreApplication::translate(
                "xuanhub.settings.errors",
                "下载的插件包与更新索引不一致"));
            QFile::remove(path);
            return;
        }
        package.insert(QStringLiteral("updateSourceId"),
                       update.value(QStringLiteral("sourceId")));
        emit pluginUpdatePackageReady(package);
    });
}

QAbstractItemModel* SettingsViewModel::pluginModel() const { return m_sortModel; }

QObject* SettingsViewModel::pluginProxy() const { return m_pluginProxy; }

bool SettingsViewModel::pluginUpdateChecking() const {
    return m_updateService && m_updateService->checking();
}

bool SettingsViewModel::pluginUpdateDownloading() const {
    return m_updateService && m_updateService->downloading();
}

int SettingsViewModel::pluginContentWidth(const QString& id) const {
    return m_barPlugins ? m_barPlugins->contentWidth(id) : 0;
}

QVariantMap SettingsViewModel::pluginHealth(const QString& id) const {
    return m_barPlugins ? m_barPlugins->health(id) : QVariantMap{};
}

bool SettingsViewModel::restartPlugin(const QString& id) {
    return m_barPlugins && m_barPlugins->restartPlugin(id);
}

QVariantMap SettingsViewModel::pluginDependencyPlan(const QString& id) const {
    return m_pluginManager ? m_pluginManager->dependencyInstallPlan(id) : QVariantMap{};
}

bool SettingsViewModel::installPluginDependencies(const QString& id) {
    if (!m_pluginManager || !m_pluginManager->installPluginDependencies(id)) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "无法启动离线依赖安装"));
        return false;
    }
    setErrorMessage({});
    return true;
}

bool SettingsViewModel::acceptPluginMigration(const QString& id) {
    return m_pluginModel->acceptMigration(id);
}

QVariantMap SettingsViewModel::inspectPluginPackage(const QString& packagePath) {
    const PluginPackageValidationResult package =
        PluginPackageValidator::validateAndExtract(packagePath);
    if (!package.ok) {
        setErrorMessage(package.error);
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), package.error},
            {QStringLiteral("path"), packagePath},
        };
    }
    const QString configDirectory = QFileInfo(m_config->activePath()).absolutePath();
    const ManagedPluginVersion current =
        PluginPackageStore(configDirectory).current(package.descriptor.id);
    setErrorMessage({});
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), packagePath},
        {QStringLiteral("id"), package.descriptor.id},
        {QStringLiteral("name"), package.descriptor.displayName},
        {QStringLiteral("version"), package.descriptor.version},
        {QStringLiteral("runtime"), package.runtime},
        {QStringLiteral("publisher"), package.publisher},
        {QStringLiteral("packageHash"), package.packageHash},
        {QStringLiteral("signatureStatus"), package.signatureStatus},
        {QStringLiteral("signatureAlgorithm"), package.signatureAlgorithm},
        {QStringLiteral("signerKeyId"), package.signerKeyId},
        {QStringLiteral("declaredPermissions"), package.declaredPermissions},
        {QStringLiteral("grantedPermissions"), package.grantedPermissions},
        {QStringLiteral("unsupportedPermissions"), package.unsupportedPermissions},
        {QStringLiteral("fileCount"), package.files.size()},
        {QStringLiteral("currentVersion"), current.version},
        {QStringLiteral("isUpdate"), !current.version.isEmpty()},
    };
}

QVariantMap SettingsViewModel::managedPluginInfo(const QString& id) const {
    const QString configDirectory = QFileInfo(m_config->activePath()).absolutePath();
    const ManagedPluginVersion current = PluginPackageStore(configDirectory).current(id);
    if (current.id.isEmpty()) return {{QStringLiteral("managed"), false}};
    QVariantMap result = current.installRecord.toVariantMap();
    result.insert(QStringLiteral("managed"), true);
    result.insert(QStringLiteral("id"), current.id);
    result.insert(QStringLiteral("version"), current.version);
    result.insert(QStringLiteral("previousVersion"), current.previousVersion);
    result.insert(QStringLiteral("rollbackAvailable"),
                  !current.previousVersion.isEmpty());
    return result;
}

bool SettingsViewModel::stopManagedPlugin(const QString& id,
                                          QStringList* affectedIds,
                                          QString* error) {
    if (!m_pluginManager || !affectedIds) return false;
    *affectedIds = m_pluginManager->dependentPluginIds(id);
    if (!affectedIds->contains(id)) affectedIds->prepend(id);
    if (m_barPlugins) {
        for (const QString& affected : std::as_const(*affectedIds))
            m_barPlugins->removePlugin(affected);
        // Loader delegate 的销毁可能走 DeferredDelete；DLL 卸载前必须让 QML
        // 放开 viewModel/pluginContext，避免受管版本切换时留下模块引用。
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    if (m_pluginManager->stopOne(id)) return true;
    if (m_barPlugins) {
        m_barPlugins->setPlugins(m_pluginManager->loaded());
        m_barPlugins->setLoadErrors(m_pluginManager->loadErrors());
    }
    if (error) {
        *error = m_pluginManager->loadError(id);
        if (error->isEmpty()) *error = QStringLiteral("cannot stop active plugin");
    }
    return false;
}

bool SettingsViewModel::startManagedPlugin(const QString& id,
                                           const QStringList& affectedIds,
                                           QString* error) {
    if (!m_pluginManager) return false;
    const QList<PluginConfigEntry> entries = m_config->pluginEntries();
    auto findEntry = [&entries](const QString& target, PluginConfigEntry* out) {
        for (const PluginConfigEntry& entry : entries) {
            if (entry.id != target) continue;
            if (out) *out = entry;
            return true;
        }
        return false;
    };
    PluginConfigEntry rootEntry;
    if (!findEntry(id, &rootEntry)) {
        if (error) *error = QStringLiteral("active plugin has no config entry");
        return false;
    }
    const QString pluginsDirectory =
        QDir(m_exeDir).filePath(QStringLiteral("plugins"));
    bool loaded = m_pluginManager->loadOne(pluginsDirectory, rootEntry);
    if (loaded) {
        for (const QString& affected : affectedIds) {
            if (affected == id || m_pluginManager->isLoaded(affected)) continue;
            PluginConfigEntry childEntry;
            if (!findEntry(affected, &childEntry)
                || !m_pluginManager->loadOne(pluginsDirectory, childEntry)) {
                loaded = false;
                if (error) *error = m_pluginManager->loadError(affected);
                break;
            }
        }
    }
    if (!loaded) {
        // 新版本可能只启动了一部分 host/子插件；回滚切指针前先恢复为彻底停止态。
        if (m_pluginManager->isLoaded(id)) m_pluginManager->stopOne(id);
        if (error && error->isEmpty()) {
            *error = m_pluginManager->loadError(id);
            if (error->isEmpty()) *error = QStringLiteral("cannot start switched plugin");
        }
        return false;
    }
    if (m_barPlugins) {
        for (const QString& affected : affectedIds) {
            const auto* plugin = m_pluginManager->loadedPlugin(affected);
            if (plugin) m_barPlugins->replacePlugin(*plugin);
        }
        m_barPlugins->setLoadErrors(m_pluginManager->loadErrors());
    }
    return true;
}

QVariantMap SettingsViewModel::installPluginPackage(const QString& packagePath) {
    if (m_pluginModel->isDirty()) {
        const QString error = QCoreApplication::translate(
            "xuanhub.settings.errors", "请先保存或放弃插件设置草稿，再管理插件包");
        setErrorMessage(error);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QStringList affectedIds;
    PluginPackageStore::ActivationHooks hooks;
    hooks.isActive = [this](const QString& id) {
        return m_pluginManager && m_pluginManager->isLoaded(id);
    };
    hooks.stop = [this, &affectedIds](const QString& id, QString* error) {
        return stopManagedPlugin(id, &affectedIds, error);
    };
    hooks.start = [this, &affectedIds](const QString& id, QString* error) {
        return startManagedPlugin(id, affectedIds, error);
    };
    PluginPackageStore store(QFileInfo(m_config->activePath()).absolutePath());
    const PluginPackageStoreResult result = store.install(packagePath, hooks);
    reloadPluginCatalog();
    setErrorMessage(result.ok ? QString() : result.error);
    return packageStoreResult(result);
}

QVariantMap SettingsViewModel::rollbackPluginPackage(const QString& id) {
    if (m_pluginModel->isDirty()) {
        const QString error = QCoreApplication::translate(
            "xuanhub.settings.errors", "请先保存或放弃插件设置草稿，再管理插件包");
        setErrorMessage(error);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QStringList affectedIds;
    PluginPackageStore::ActivationHooks hooks;
    hooks.isActive = [this](const QString& target) {
        return m_pluginManager && m_pluginManager->isLoaded(target);
    };
    hooks.stop = [this, &affectedIds](const QString& target, QString* error) {
        return stopManagedPlugin(target, &affectedIds, error);
    };
    hooks.start = [this, &affectedIds](const QString& target, QString* error) {
        return startManagedPlugin(target, affectedIds, error);
    };
    PluginPackageStore store(QFileInfo(m_config->activePath()).absolutePath());
    const PluginPackageStoreResult result = store.rollback(id, hooks);
    reloadPluginCatalog();
    setErrorMessage(result.ok ? QString() : result.error);
    return packageStoreResult(result);
}

QVariantMap SettingsViewModel::uninstallPluginPackage(const QString& id,
                                                       bool deleteUserData) {
    if (m_pluginModel->isDirty()) {
        const QString error = QCoreApplication::translate(
            "xuanhub.settings.errors", "请先保存或放弃插件设置草稿，再管理插件包");
        setErrorMessage(error);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }
    QStringList affectedIds;
    PluginPackageStore::ActivationHooks hooks;
    hooks.isActive = [this](const QString& target) {
        return m_pluginManager && m_pluginManager->isLoaded(target);
    };
    hooks.stop = [this, &affectedIds](const QString& target, QString* error) {
        return stopManagedPlugin(target, &affectedIds, error);
    };
    hooks.start = [this, &affectedIds](const QString& target, QString* error) {
        return startManagedPlugin(target, affectedIds, error);
    };
    PluginPackageStore store(QFileInfo(m_config->activePath()).absolutePath());
    const PluginPackageStoreResult result =
        store.uninstall(id, deleteUserData, hooks);
    reloadPluginCatalog();
    setErrorMessage(result.ok ? QString() : result.error);
    return packageStoreResult(result);
}

void SettingsViewModel::checkPluginUpdates() {
    QStringList sources;
    for (const QString& line : m_updateSources.split(QLatin1Char('\n'))) {
        const QString source = line.trimmed();
        if (!source.isEmpty() && !sources.contains(source)) sources.append(source);
    }
    if (sources.isEmpty()) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "请先添加至少一个插件更新源"));
        return;
    }
    QVariantMap installed;
    for (int row = 0; row < m_pluginModel->rowCount(); ++row) {
        const QModelIndex index = m_pluginModel->index(row);
        if (!m_pluginModel->data(index, SettingsPluginModel::DiscoveredRole).toBool())
            continue;
        const QString id = m_pluginModel->data(index, SettingsPluginModel::IdRole).toString();
        const QString version = m_pluginModel->data(
            index, SettingsPluginModel::VersionRole).toString();
        if (id.isEmpty() || version.isEmpty()) continue;
        const QString kind = m_pluginModel->data(
            index, SettingsPluginModel::KindRole).toString();
        const QVariantMap managed = managedPluginInfo(id);
        installed.insert(id, QVariantMap{
            {QStringLiteral("version"), version},
            {QStringLiteral("runtime"), kind == QLatin1String("python")
                ? QStringLiteral("python") : QStringLiteral("native")},
            {QStringLiteral("signerKeyId"),
             managed.value(QStringLiteral("signerKeyId"))},
        });
    }
    setErrorMessage({});
    m_updateService->check(sources, installed);
}

void SettingsViewModel::downloadPluginUpdate(const QString& id,
                                             const QString& version) {
    if (m_updateService) m_updateService->download(id, version);
}

void SettingsViewModel::clearPluginUpdates() {
    if (m_availablePluginUpdates.isEmpty() && m_pluginUpdateErrors.isEmpty()) return;
    m_availablePluginUpdates.clear();
    m_pluginUpdateErrors.clear();
    emit availablePluginUpdatesChanged();
    emit pluginUpdateErrorsChanged();
}

QStringList SettingsViewModel::themePackNames() const {
    return m_registry ? m_registry->packNames() : QStringList{};
}

QStringList SettingsViewModel::themePackIds() const {
    if (!m_registry) return {};
    QStringList ids;
    ids.reserve(m_registry->packs().size());
    for (const auto& p : m_registry->packs()) ids.append(p.id);
    return ids;
}

void SettingsViewModel::setBarHeight(int v) {
    if (m_barHeight == v) return;
    m_barHeight = v;
    emit barHeightChanged();
}

void SettingsViewModel::setBarOpacity(int v) {
    // 钳制到 20-100（下限 20 防 bar 完全看不见）
    v = qBound(20, v, 100);
    if (m_barOpacity == v) return;
    m_barOpacity = v;
    emit barOpacityChanged();
}

void SettingsViewModel::setBarEdge(const QString& v) {
    if (m_barEdge == v) return;
    m_barEdge = v;
    emit barEdgeChanged();
}

void SettingsViewModel::setBarMode(const QString& v) {
    if (m_barMode == v) return;
    m_barMode = v;
    emit barModeChanged();
}

void SettingsViewModel::setTheme(const QString& v) {
    if (m_theme == v) return;
    m_theme = v;
    emit themeChanged();
}

void SettingsViewModel::setThemeId(const QString& v) {
    if (m_themeId == v) return;
    m_themeId = v;
    emit themeIdChanged();
}

void SettingsViewModel::setAutoStart(bool v) {
    if (m_autoStart == v) return;
    m_autoStart = v;
    emit autoStartChanged();
}

void SettingsViewModel::setLanguage(const QString& v) {
    const QString normalized = LocaleManager::normalizeRequested(v);
    if (m_language == normalized) return;
    m_language = normalized;
    emit languageChanged();
}

void SettingsViewModel::setUpdateSources(const QString& v) {
    if (m_updateSources == v) return;
    m_updateSources = v;
    emit updateSourcesChanged();
}

void SettingsViewModel::reloadThemePacks() {
    // 真正扫盘（用户可能新装/卸载了主题包），再发 NOTIFY 让 QML 重新读取 names/ids
    if (m_registry)
        m_registry->reload(QDir(m_exeDir).absoluteFilePath(QStringLiteral("themes")));
    emit themePacksReloaded();
}

void SettingsViewModel::reloadPluginCatalog() {
    m_pluginModel->reload(m_config->pluginEntries(),
                          m_config->rawConfig().value(QStringLiteral("plugins")).toArray(),
                          m_exeDir, m_config->activePath());
    if (!m_pluginManager) return;
    for (const PluginConfigEntry& entry : m_config->pluginEntries()) {
        const auto* suggestion = m_pluginManager->migrationSuggestion(entry.id);
        if (!suggestion) continue;
        m_pluginModel->setMigrationSuggestion(
            entry.id, suggestion->targetVersion,
            suggestion->config, suggestion->error);
    }
}

void SettingsViewModel::reloadFromConfig() {
    setBarHeight(m_config->barHeight());
    // config 存 0.0-1.0 qreal，UI 用 0-100 int（四舍五入）
    setBarOpacity(qRound(m_config->barOpacity() * 100));
    setBarEdge(m_config->barEdge());
    setBarMode(m_config->barMode());
    // 主题草稿取运行时值（ThemeManager）而非文件旧值（ConfigService）：
    // 托盘「切换深色/浅色」是纯运行时切换不写文件，若此处读文件旧值，用户改了别的设置
    // 保存时会把草稿里的旧 mode 一并写回 → 热重载把用户临时切的模式打回文件值。
    setTheme(m_themeMgr ? m_themeMgr->mode() : m_config->theme());
    setThemeId(m_themeMgr ? m_themeMgr->themeId() : m_config->themeId());
    setAutoStart(autostart::isEnabled());
    setLanguage(m_config->language());
    QStringList updateSources;
    const QJsonArray sourceArray = m_config->rawConfig()
        .value(QStringLiteral("pluginUpdates")).toObject()
        .value(QStringLiteral("sources")).toArray();
    for (const QJsonValue& value : sourceArray) {
        if (value.isString() && !value.toString().trimmed().isEmpty())
            updateSources.append(value.toString().trimmed());
    }
    setUpdateSources(updateSources.join(QLatin1Char('\n')));
    clearPluginUpdates();
    reloadPluginCatalog();
    // 快照配置指纹：窗口打开期间的草稿以此为底，apply 前据此判定外部改动
    m_openFingerprint = currentFingerprint();
    setErrorMessage({});
}

QString SettingsViewModel::currentFingerprint() const {
    // 只取本窗口会写回的内容：ui.language + bar 段已知键 + plugins 数组原始 JSON（含手写未知键，
    // 外部只改未知键同样算外部改动）。bar 未知键（如 monitor）本窗口不动，不入指纹
    const QJsonObject root = m_config->rawConfig();
    const QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    QJsonObject barKeys;
    for (const char* k : {"height", "edge", "mode", "opacity", "theme", "themeId"})
        barKeys.insert(QString::fromLatin1(k), bar.value(QString::fromLatin1(k)));
    QJsonObject fp;
    QJsonObject uiKeys;
    const QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    uiKeys.insert(QStringLiteral("language"), ui.value(QStringLiteral("language")));
    fp[QStringLiteral("ui")] = uiKeys;
    fp[QStringLiteral("bar")] = barKeys;
    fp[QStringLiteral("plugins")] = root.value(QStringLiteral("plugins")).toArray();
    fp[QStringLiteral("pluginUpdates")] = root.value(QStringLiteral("pluginUpdates"));
    return QString::fromUtf8(QJsonDocument(fp).toJson(QJsonDocument::Compact));
}

bool SettingsViewModel::apply() {
    // 草稿过期保护：窗口打开后配置被外部改动（热重载/手动编辑）则拒绝保存——
    // 草稿是打开时的快照，继续保存会用旧草稿把外部改动整体覆盖、静默回滚
    if (currentFingerprint() != m_openFingerprint) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors",
            "配置已被外部修改（热重载/手动编辑），请关闭设置窗口重开后再保存"));
        return false;
    }
    // 校验（QML 控件已限范围，这里双保险——草稿也可能被外部代码改动）
    if (m_barHeight < 24 || m_barHeight > 200) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "状态栏高度需在 24–200 之间"));
        return false;
    }
    if (m_barEdge != QStringLiteral("top") && m_barEdge != QStringLiteral("bottom")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "停靠边缘仅支持 top / bottom"));
        return false;
    }
    if (m_barMode != QStringLiteral("appbar") && m_barMode != QStringLiteral("taskbar")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "工作模式仅支持 appbar / taskbar"));
        return false;
    }
    if (m_theme != QStringLiteral("dark") && m_theme != QStringLiteral("light")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "主题模式仅支持 dark / light"));
        return false;
    }
    if (m_language != QStringLiteral("system") && m_language != QStringLiteral("zh-CN")
        && m_language != QStringLiteral("en-US")) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "界面语言配置无效"));
        return false;
    }
    QJsonArray updateSources;
    QSet<QString> seenUpdateSources;
    for (const QString& line : m_updateSources.split(QLatin1Char('\n'))) {
        const QString source = line.trimmed();
        if (source.isEmpty() || seenUpdateSources.contains(source)) continue;
        QString sourceError;
        if (!PluginUpdateIndex::validateSourceUrl(QUrl(source), &sourceError)) {
            setErrorMessage(QCoreApplication::translate(
                "xuanhub.settings.errors", "插件更新源无效：") + sourceError);
            return false;
        }
        seenUpdateSources.insert(source);
        updateSources.append(source);
    }
    // 校验主题包存在（防 config 手写错 id）
    if (m_registry && m_registry->find(m_themeId) == nullptr) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "主题包不存在：") + m_themeId);
        return false;
    }
    QJsonArray plugins;
    QString err;
    SecretStore secretStore(QFileInfo(m_config->activePath()).absolutePath());
    auto secretTransaction = secretStore.beginTransaction();
    if (m_pluginModel->isDirty()) {
        if (!m_pluginModel->toPluginEntriesJson(&plugins, &err, &secretTransaction)) {
            setErrorMessage(err);
            return false;
        }
        // Schema 只能描述通用约束；原生插件还可执行自身的业务终校。此入口只调用
        // validateConfig，不应用配置，保存失败时运行中的插件状态保持不变。
        if (m_pluginManager) {
            for (const QJsonValue& value : plugins) {
                const QJsonObject object = value.toObject();
                const QString id = object.value(QStringLiteral("id")).toString();
                const auto validation = m_pluginManager->validateConfig(
                    id, object.value(QStringLiteral("config")).toObject().toVariantMap());
                if (validation.supported && !validation.valid) {
                    setErrorMessage(QCoreApplication::translate(
                        "xuanhub.settings.errors", "插件 %1 拒绝配置：%2")
                                        .arg(id, validation.error));
                    return false;
                }
            }
        }
    } else {
        // 插件页没有实际编辑时保留原始数组。模型加载阶段会为旧中文枚举建立
        // 稳定值草稿，但语言/主题保存不应顺带写回这些迁移并触发插件全量重载。
        plugins = m_config->rawConfig().value(QStringLiteral("plugins")).toArray();
    }
    // 只改已知键：基于 rawConfig 组装，未知键（如 bar.monitor）原样保留
    QJsonObject root = m_config->rawConfig();
    QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    bar[QStringLiteral("height")] = m_barHeight;
    bar[QStringLiteral("edge")] = m_barEdge;
    bar[QStringLiteral("mode")] = m_barMode;     // 工作模式 appbar/taskbar（M11）
    bar[QStringLiteral("opacity")] = m_barOpacity / 100.0;   // int 0-100 → qreal 0.0-1.0
    bar[QStringLiteral("theme")] = m_theme;          // 模式 dark/light
    bar[QStringLiteral("themeId")] = m_themeId;      // 主题包 id
    root[QStringLiteral("bar")] = bar;
    QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    ui[QStringLiteral("language")] = m_language;
    root[QStringLiteral("ui")] = ui;
    root[QStringLiteral("plugins")] = plugins;
    QJsonObject pluginUpdates = root.value(QStringLiteral("pluginUpdates")).toObject();
    pluginUpdates[QStringLiteral("sources")] = updateSources;
    root[QStringLiteral("pluginUpdates")] = pluginUpdates;
    // 密钥库先原子提交；若随后 config 提交失败，事务恢复旧密钥文件。只有两个文件
    // 都成功后才 finalize，避免出现新引用配旧密钥或旧引用配新密钥的半提交状态。
    if (!secretTransaction.commit()) {
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "无法安全保存插件凭据"));
        return false;
    }
    if (!m_config->save(root)) {
        if (!secretTransaction.rollback())
            qCritical() << "[Settings] config save failed and secret rollback also failed";
        setErrorMessage(QCoreApplication::translate(
            "xuanhub.settings.errors", "写入 config.json 失败（权限/磁盘，详见日志）"));
        return false;
    }
    secretTransaction.finalize();
    autostart::setEnabled(m_autoStart);
    // 更新指纹到刚保存的状态：本次保存触发的热重载不算外部修改
    //（save() 已把 m_raw 更新为同一内容，此处重算即新快照）
    m_openFingerprint = currentFingerprint();
    setErrorMessage({});
    qDebug() << "[Settings] applied:"
             << "height=" << m_barHeight << "edge=" << m_barEdge
             << "barMode=" << m_barMode
             << "themeId=" << m_themeId << "theme=" << m_theme
             << "language=" << m_language
             << "autoStart=" << m_autoStart << "plugins=" << plugins.size();
    return true;
}

void SettingsViewModel::setErrorMessage(const QString& m) {
    if (m_errorMessage == m) return;
    m_errorMessage = m;
    emit errorMessageChanged();
}
