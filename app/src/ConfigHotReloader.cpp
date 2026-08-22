// ConfigHotReloader.cpp
#include "ConfigHotReloader.h"

#include "BarWindow.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "PluginManager.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "LocaleManager.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
// plugins 段指纹：条目按文件序序列化为紧凑 JSON。
// 用于判断"这次保存到底动没动插件配置"——没动就不做全量重载
QString pluginsFingerprint(const QList<PluginConfigEntry>& entries) {
    QJsonArray arr;
    for (const auto& e : entries) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), e.id);
        o.insert(QStringLiteral("position"), e.position);
        o.insert(QStringLiteral("order"), e.order);
        o.insert(QStringLiteral("width"), e.width);
        o.insert(QStringLiteral("config"), QJsonObject::fromVariantMap(e.config));
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
} // namespace

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
    m_pluginsFingerprint = pluginsFingerprint(m_config->pluginEntries());
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

    // 插件全量重载：仅在 plugins 段真的变了（或托盘强制）时执行——改主题色、
    // 高度这类无关变更不再摧毁所有插件状态（打开的弹窗、内部计时、Python
    // 子插件运行时）。顺序铁律：先 shutdown 删光插件对象再 load，
    // 全程在同一槽函数内完成（不经事件循环），QML 不会访问悬空 viewModel；
    // setPlugins 内部 begin/endResetModel 驱动 ListView 重建 delegate
    const QString fp = pluginsFingerprint(m_config->pluginEntries());
    if (!force && fp == m_pluginsFingerprint) {
        // 高度/边缘/模式仍可能变化：无插件重载时直接重新协商并定位。
        m_window->setupBar();
        qDebug() << "[Config] plugins section unchanged, skip plugin reload";
        return;
    }
    m_pluginsFingerprint = fp;
    // 模型中的 Loader/delegate 持有插件 ViewModel 的元对象，而这些元对象代码位于
    // 插件 DLL。必须先让 QML 解除引用并处理延迟删除，再 shutdown/unload；旧顺序
    // 先卸 DLL、后 reset model，会在 delegate 清理时跳入已卸载代码导致崩溃。
    m_model->setPlugins({});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    m_pm->shutdownAll();
    m_pm->loadFrom(QDir(m_exeDir).absoluteFilePath(QStringLiteral("plugins")),
                   m_config->pluginEntries());
    m_model->setPlugins(m_pm->loaded());
    m_window->setupBar();
}
