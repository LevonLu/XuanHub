// ConfigHotReloader.cpp
#include "ConfigHotReloader.h"

#include "BarWindow.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "PluginManager.h"
#include "PluginChangeSet.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "LocaleManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <utility>

ConfigHotReloader::ConfigHotReloader(ConfigService* config, ThemeManager* theme,
                                     ThemeRegistry* registry,
                                     PluginManager* pm, PluginListModel* model,
                                     BarWindow* window, LocaleManager* locale,
                                     QString exeDir, QString configDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_pm(pm), m_model(model), m_window(window),
      m_locale(locale),
      m_exeDir(std::move(exeDir)), m_configDir(std::move(configDir)) {
    // 防抖：一次保存常触发多个 fileChanged（写内容 + 改属性），合并为一次 reload
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &ConfigHotReloader::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        // 编辑器常用「临时文件 + 重命名」保存，原路径的监视会随之失效 → 先重新挂上
        armWatcher();
        m_debounce.start();
    });
    // 文件缺失期退而监视父目录（见 armWatcher）：文件恢复时目录变化触发重挂 + 重载
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        armWatcher();
        m_debounce.start();
    });
    armWatcher();
    // 构造晚于 main() 的首次 loadFrom：以当前生效的插件配置为基准指纹，
    // 之后 watcher 触发的 reload 才能识别"plugins 段其实没变"
    m_pluginsFingerprint = PluginConfigDiff::entriesFingerprint(m_config->pluginEntries());
    m_pluginEntries = m_config->pluginEntries();
}

void ConfigHotReloader::armWatcher() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString p = m_config->activePath();
    if (p.isEmpty()) return;
    if (QFile::exists(p)) {
        m_watcher.addPath(p);
    } else {
        // 文件缺失（被删/重命名且未即刻重建）：监视父目录兜底，
        // 否则 watcher 全空、此后再改 config 都不会触发热重载（实测失挂路径）
        const QString dir = QFileInfo(p).absolutePath();
        if (QDir(dir).exists())
            m_watcher.addPath(dir);
    }
}

void ConfigHotReloader::reload() { reloadImpl(false); }

void ConfigHotReloader::forceReload() { reloadImpl(true); }

void ConfigHotReloader::reloadImpl(bool force) {
    qDebug() << "[Config] reload triggered" << (force ? "(force)" : "");
    // 先重挂监视：即使本次 load 失败提前返回，也不让 watcher 停在失挂状态
    armWatcher();
    // 解析失败（文件缺失/JSON 写坏）→ ConfigService 保留旧值，本次不动任何东西
    if (!m_config->load(m_exeDir, m_configDir)) {
        qWarning() << "[Config] reload failed, keep current settings";
        return;
    }
    armWatcher();  // 生效路径可能换边（exeDir ↔ configDir）

    // languageChanged 已在 ConfigService::load() 内同步触发；强制重载时即使语言值
    // 未变也重扫插件 i18n 目录，方便开发者更新语言包后从托盘立即验证。
    if (force && m_locale) m_locale->reloadCatalogs();

    // 主题包：重扫 themes/（用户可能新装/卸载了主题包），再按文件值同步当前主题
    if (m_registry) m_registry->reload(QDir(m_exeDir).absoluteFilePath(QStringLiteral("themes")));

    // 主题：文件是事实源，覆盖托盘「切换深色/浅色」的运行时切换（托盘切换不写回文件）
    if (m_theme->themeId() != m_config->themeId()
        || m_theme->mode() != m_config->theme()) {
        m_theme->applyTheme(m_config->themeId(), m_config->theme());
    } else {
        // id/mode 没变也可能只改了当前主题包的 theme.json（registry 刚重扫）→ 重解析色板
        m_theme->refresh();
    }

    const QList<PluginConfigEntry> nextEntries = m_config->pluginEntries();
    const QString fp = PluginConfigDiff::entriesFingerprint(nextEntries);
    if (!force && fp == m_pluginsFingerprint) {
        // 高度/边缘/模式仍可能变化：无插件重载时直接重新协商并定位。
        m_window->setupBar();
        qDebug() << "[Config] plugins section unchanged, skip plugin reload";
        return;
    }
    const PluginChangeSet changes = PluginConfigDiff::calculate(m_pluginEntries, nextEntries);
    m_pluginsFingerprint = fp;
    m_pluginEntries = nextEntries;

    const QString pluginsDir = QDir(m_exeDir).absoluteFilePath(QStringLiteral("plugins"));
    if (!force) {
        QHash<QString, PluginConfigEntry> nextById;
        for (const auto& entry : nextEntries) nextById.insert(entry.id, entry);

        QSet<QString> restartConfigIds;
        for (const QString& id : changes.configChanged) {
            const auto entryIt = nextById.constFind(id);
            if (entryIt == nextById.cend()) continue;
            const auto result = m_pm->applyConfig(entryIt.value());
            if (result == PluginManager::ConfigApplyResult::Unsupported)
                restartConfigIds.insert(id);
        }

        // host 被移除或重启时，其当前子插件也必须先从 QML 模型解除引用，并在
        // host 重新加载后按现配置恢复；普通目标插件不会波及任何同级实例。
        QSet<QString> stopIds;
        for (const QString& id : changes.removed) stopIds.insert(id);
        for (const QString& id : std::as_const(restartConfigIds)) stopIds.insert(id);
        const QStringList initialStopIds = stopIds.values();
        for (const QString& id : initialStopIds) {
            for (const QString& child : m_pm->dependentPluginIds(id)) stopIds.insert(child);
        }

        QStringList childStops;
        QStringList parentStops;
        for (const QString& id : std::as_const(stopIds)) {
            if (m_pm->dependentPluginIds(id).isEmpty()) childStops.append(id);
            else parentStops.append(id);
            m_model->removePlugin(id);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        bool incrementalSafe = true;
        for (const QString& id : childStops + parentStops) {
            if (!m_pm->stopOne(id)) {
                qWarning() << "[Config] incremental stop failed, fallback to full reload:" << id;
                incrementalSafe = false;
                break;
            }
        }
        if (incrementalSafe) {
            QSet<QString> loadIds;
            for (const QString& id : changes.added) loadIds.insert(id);
            for (const QString& id : std::as_const(restartConfigIds)) loadIds.insert(id);
            for (const QString& id : std::as_const(stopIds)) {
                if (nextById.contains(id)) loadIds.insert(id);
            }

            // 子插件可能排在 host 前面。失败项保留到下一轮；某一轮加载了 host 后，
            // 下一轮即可由该 host 创建子实例。无进展时停止，避免坏插件无限重试。
            QSet<QString> pending = loadIds;
            while (!pending.isEmpty()) {
                bool progressed = false;
                const auto currentPending = pending;
                for (const QString& id : currentPending) {
                    const auto entryIt = nextById.constFind(id);
                    if (entryIt == nextById.cend()) {
                        pending.remove(id);
                        continue;
                    }
                    if (!m_pm->loadOne(pluginsDir, entryIt.value())) continue;
                    if (const auto* loaded = m_pm->loadedPlugin(id))
                        m_model->insertPlugin(*loaded);
                    pending.remove(id);
                    progressed = true;
                }
                if (!progressed) break;
            }
            for (const QString& id : std::as_const(pending))
                qWarning() << "[Config] target plugin failed to load, peers kept running:" << id;

            m_model->setLoadErrors(m_pm->loadErrors());

            for (const QString& id : changes.layoutChanged) {
                const auto entryIt = nextById.constFind(id);
                if (entryIt == nextById.cend()) continue;
                m_pm->updateLayout(entryIt.value());
                m_model->updateLayout(id, entryIt->position,
                                      entryIt->order, entryIt->width);
            }
            m_window->setupBar();
            qDebug() << "[Config] incremental plugin reload applied"
                     << "added=" << changes.added
                     << "removed=" << changes.removed
                     << "layout=" << changes.layoutChanged
                     << "config=" << changes.configChanged;
            return;
        }
    }

    // 托盘强制重载以及旧 host 无法增量销毁时保留全量灾难回退路径。
    // 模型中的 Loader/delegate 持有插件 ViewModel 的元对象，而这些元对象代码位于
    // 插件 DLL。必须先让 QML 解除引用并处理延迟删除，再 shutdown/unload；旧顺序
    // 先卸 DLL、后 reset model，会在 delegate 清理时跳入已卸载代码导致崩溃。
    m_model->setPlugins({});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    m_pm->shutdownAll();
    m_pm->loadFrom(pluginsDir, nextEntries);
    m_model->setPlugins(m_pm->loaded());
    m_model->setLoadErrors(m_pm->loadErrors());
    m_window->setupBar();
}
