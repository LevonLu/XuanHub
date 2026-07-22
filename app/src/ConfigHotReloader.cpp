// ConfigHotReloader.cpp
#include "ConfigHotReloader.h"

#include "BarWindow.h"
#include "ConfigService.h"
#include "PluginListModel.h"
#include "PluginManager.h"
#include "ThemeManager.h"
#include "ThemeRegistry.h"

#include <QDebug>
#include <QDir>
#include <QFile>

ConfigHotReloader::ConfigHotReloader(ConfigService* config, ThemeManager* theme,
                                     ThemeRegistry* registry,
                                     PluginManager* pm, PluginListModel* model,
                                     BarWindow* window,
                                     QString exeDir, QString configDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_pm(pm), m_model(model), m_window(window),
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
    armWatcher();
}

void ConfigHotReloader::armWatcher() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    const QString p = m_config->activePath();
    if (!p.isEmpty() && QFile::exists(p))
        m_watcher.addPath(p);
}

void ConfigHotReloader::reload() {
    qDebug() << "[Config] reload triggered";
    // 解析失败（文件缺失/JSON 写坏）→ ConfigService 保留旧值，本次不动任何东西
    if (!m_config->load(m_exeDir, m_configDir)) {
        qWarning() << "[Config] reload failed, keep current settings";
        return;
    }
    armWatcher();  // 生效路径可能换边（exeDir ↔ configDir）

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

    // 高度/边缘：重新协商 AppBar 并定位窗口（幂等；值没变也无副作用）
    m_window->setupBar();

    // 插件全量重载：先 shutdown 删光插件对象再 load，
    // 全程在同一槽函数内完成（不经事件循环），QML 不会访问悬空 viewModel；
    // setPlugins 内部 begin/endResetModel 驱动 ListView 重建 delegate
    m_pm->shutdownAll();
    m_pm->loadFrom(QDir(m_exeDir).absoluteFilePath(QStringLiteral("plugins")),
                   m_config->pluginEntries());
    m_model->setPlugins(m_pm->loaded());
}
