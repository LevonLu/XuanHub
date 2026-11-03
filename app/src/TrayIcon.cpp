// TrayIcon.cpp
#include "TrayIcon.h"
#include "ThemeManager.h"
#include "ConfigService.h"
#include "AutoStart.h"
#include "LocaleManager.h"
#include "PluginActionRegistry.h"
#include "barplugin/EventBus.h"

#include <QJsonObject>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSignalBlocker>
#include <QTimer>

TrayIcon::TrayIcon(ThemeManager* theme, xuanhub::EventBus* bus,
                   ConfigService* config, LocaleManager* locale, QObject* parent)
    : QObject(parent), m_theme(theme), m_config(config), m_bus(bus), m_locale(locale),
      m_menu(nullptr), m_autoStartAction(nullptr), m_taskbarModeAction(nullptr) {
    m_tray = new QSystemTrayIcon(this);
    refreshIcon();   // 首次设置品牌图标

    // 保留主题变化刷新入口；ThemeManager 在主线程，AutoConnection 排队到 GUI 线程安全。
    // 品牌图标当前为固定配色，后续若增加主题化变体可继续复用此入口。
    if (m_theme) connect(m_theme, &ThemeManager::themeChanged, this, &TrayIcon::refreshIcon);

    // 插件经 EventBus 请求的气泡提示（如 pybridge 未检测到 Python 环境）。
    // 注意 slot 用裸签名串而非 SLOT() 宏：EventBus 经 QMetaObject::invokeMethod
    // 投递，后者不接受 SLOT() 的 "1" 前缀（会报 No such method）。
    if (bus) bus->subscribe(QStringLiteral("app.notify"), this, "onAppNotify(QVariantMap)");
    if (bus) {
        // bar 上的玄枢菜单是普通插件，不能依赖宿主类型；通过 EventBus 请求动作，
        // 实际执行仍落到托盘控制器，保证两个入口的行为和状态来源一致。
        bus->subscribeV2(QStringLiteral("app.quickMenu.command"), this,
                       "onQuickMenuCommand(QVariantMap)");
        bus->subscribeV2(QStringLiteral("app.quickMenu.requestState"), this,
                       "publishQuickMenuState(QVariantMap)");
    }

    m_menu = new QMenu();

    // 设置窗口（M8）
    m_settingsAction = m_menu->addAction(QString());
    connect(m_settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    m_autoStartAction = m_menu->addAction(QString());
    m_autoStartAction->setCheckable(true);
    connect(m_autoStartAction, &QAction::toggled, this, &TrayIcon::setAutoStartEnabled);
    // 勾选状态与注册表现状对齐（阻断信号，避免初始化时回写一遍）；
    // 菜单每次弹出前再对齐一次 —— 设置窗口也可能改同一注册表键
    {
        QSignalBlocker blocker(m_autoStartAction);
        m_autoStartAction->setChecked(autostart::isEnabled());
    }
    connect(m_menu, &QMenu::aboutToShow, this, [this]() {
        {
            QSignalBlocker blocker(m_autoStartAction);
            m_autoStartAction->setChecked(autostart::isEnabled());
        }
        // 勾选状态与当前工作模式对齐（设置窗口/手改配置也可能切模式）
        if (m_config) {
            QSignalBlocker blocker(m_taskbarModeAction);
            m_taskbarModeAction->setChecked(
                m_config->barMode() == QStringLiteral("taskbar"));
        }
    });

    // 工作模式开关（M11）：写回 config.json 的 bar.mode，生效全靠 M7 watcher 热重载
    m_taskbarModeAction = m_menu->addAction(QString());
    m_taskbarModeAction->setCheckable(true);
    connect(m_taskbarModeAction, &QAction::toggled, this, &TrayIcon::setTaskbarMode);
    if (m_config) {
        QSignalBlocker blocker(m_taskbarModeAction);
        m_taskbarModeAction->setChecked(m_config->barMode() == QStringLiteral("taskbar"));
    }

    m_toggleThemeAction = m_menu->addAction(QString());
    connect(m_toggleThemeAction, &QAction::triggered, [this]() {
        // 仅切 light/dark 模式，保持当前主题包（switchMode 不写回文件，热重载时会回滚）
        const QString t = (m_theme->mode() == QStringLiteral("dark"))
                              ? QStringLiteral("light") : QStringLiteral("dark");
        m_theme->switchMode(t);
    });

    // 配置热重载（M7）：手动触发入口，与文件监听同一个 reload()
    m_reloadAction = m_menu->addAction(QString());
    connect(m_reloadAction, &QAction::triggered, this, &TrayIcon::reloadRequested);

    // 插件动作使用独立子菜单，内容由 Manifest 声明和运行时状态共同决定。
    // 未安装带 tray 动作的插件时隐藏入口，不占用原有菜单空间。
    m_pluginActionsMenu = m_menu->addMenu(QString());
    m_pluginActionsMenu->menuAction()->setVisible(false);

    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, []() { QCoreApplication::quit(); });

    if (m_locale)
        connect(m_locale, &LocaleManager::localeChanged, this, &TrayIcon::retranslate);
    retranslate();

    m_tray->setContextMenu(m_menu);
    // 菜单插件可在稍后加载并回放状态；主题热切换也应更新已打开的菜单。
    if (m_theme) connect(m_theme, &ThemeManager::themeChanged, this,
                         [this] { publishQuickMenuState(); });
    if (m_config) connect(m_config, &ConfigService::barModeChanged, this,
                          [this] { publishQuickMenuState(); });
    publishQuickMenuState();
}

void TrayIcon::retranslate() {
    m_tray->setToolTip(QCoreApplication::translate(
        "xuanhub.tray", "玄枢 XuanHub"));
    m_settingsAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "设置..."));
    m_autoStartAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "开机自启"));
    m_taskbarModeAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "任务栏模式"));
    m_toggleThemeAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "切换深色/浅色"));
    m_reloadAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "重新加载配置"));
    m_pluginActionsMenu->setTitle(QCoreApplication::translate(
        "xuanhub.tray", "插件操作"));
    m_quitAction->setText(QCoreApplication::translate(
        "xuanhub.tray", "退出"));
}

void TrayIcon::show() { m_tray->show(); }

void TrayIcon::setPluginActions(PluginActionRegistry* actions) {
    if (m_pluginActions == actions) return;
    if (m_pluginActions) disconnect(m_pluginActions, nullptr, this, nullptr);
    m_pluginActions = actions;
    if (m_pluginActions) {
        // 动作可能在触发过程中同步改变状态；排队重建可避免删除仍在派发
        // triggered 信号的 QAction。
        connect(m_pluginActions, &PluginActionRegistry::actionsChanged,
                this, &TrayIcon::rebuildPluginActions, Qt::QueuedConnection);
    }
    rebuildPluginActions();
}

void TrayIcon::rebuildPluginActions() {
    if (!m_pluginActionsMenu) return;
    m_pluginActionsMenu->clear();
    const QVariantList actions = m_pluginActions
        ? m_pluginActions->actions(QStringLiteral("tray")) : QVariantList{};
    for (const QVariant& value : actions) {
        const QVariantMap item = value.toMap();
        auto* action = m_pluginActionsMenu->addAction(
            item.value(QStringLiteral("label")).toString());
        action->setEnabled(item.value(QStringLiteral("enabled"), true).toBool());
        action->setCheckable(item.value(QStringLiteral("checkable")).toBool());
        action->setChecked(item.value(QStringLiteral("checked")).toBool());
        const QString pluginId = item.value(QStringLiteral("pluginId")).toString();
        const QString actionId = item.value(QStringLiteral("actionId")).toString();
        connect(action, &QAction::triggered, this, [this, pluginId, actionId] {
            if (m_pluginActions) m_pluginActions->invoke(pluginId, actionId);
        });
    }
    m_pluginActionsMenu->menuAction()->setVisible(!actions.isEmpty());
}

TrayIcon::~TrayIcon() {
    // m_menu 以无父方式创建（setContextMenu 不接管所有权），须显式释放
    delete m_menu;
}

void TrayIcon::refreshIcon() {
    // 托盘只有 16–24 px：使用去掉端点层级、星形和渐变的紧凑 X 版本；
    // 完整品牌图标缩到这里会变成零散亮点，并在浅色任务栏上显得过轻。
    const QIcon brandIcon(QStringLiteral(
        ":/qt/qml/XuanHub/resources/xuanhub-tray-icon-64.png"));
    if (!brandIcon.isNull()) {
        m_tray->setIcon(brandIcon);
        return;
    }

    // 资源异常时保留原有动态绘制作为兜底，避免托盘入口完全不可见。
    // 从当前主题色板取色绘制托盘图标（底色=bg，边框=accent，文字=fg）
    // 主题不存在或缺字段时给深色兜底
    QColor bg     = QColor(QStringLiteral("#202020"));
    QColor accent = QColor(QStringLiteral("#60cdff"));
    QColor fg     = QColor(QStringLiteral("#ffffff"));
    if (m_theme) {
        const QVariantMap c = m_theme->colors();
        if (c.contains(QStringLiteral("bg")))     bg     = c.value(QStringLiteral("bg")).value<QColor>();
        if (c.contains(QStringLiteral("accent"))) accent = c.value(QStringLiteral("accent")).value<QColor>();
        if (c.contains(QStringLiteral("fg")))     fg     = c.value(QStringLiteral("fg")).value<QColor>();
    }
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(QPen(accent, 2));
    p.drawRoundedRect(4, 4, 24, 24, 6, 6);
    p.setPen(QPen(fg, 2));
    p.drawText(QRect(4, 4, 24, 24), Qt::AlignCenter, QStringLiteral("XH"));
    p.end();
    m_tray->setIcon(QIcon(px));
}

void TrayIcon::recreate() {
    // Explorer 重启后托盘图标可能丢失，先隐藏再显示重建
    m_tray->hide();
    m_tray->show();
}

void TrayIcon::setAutoStartEnabled(bool on) {
    autostart::setEnabled(on);   // 逻辑收编到 AutoStart.h（设置窗口共用）
}

void TrayIcon::setTaskbarMode(bool on) {
    if (!m_config) return;
    // 只改 bar.mode 一个键，其余配置（含未知键）经 m_raw 原样保留
    QJsonObject root = m_config->rawConfig();
    QJsonObject bar = root.value(QStringLiteral("bar")).toObject();
    bar.insert(QStringLiteral("mode"),
               on ? QStringLiteral("taskbar") : QStringLiteral("appbar"));
    root.insert(QStringLiteral("bar"), bar);
    // 生效走 M7 文件监听热重载（watcher 盯 activePath），本函数不直接动窗口
    m_config->save(root);
}

void TrayIcon::onAppNotify(QVariantMap payload) {
    const QString title = payload.value(QStringLiteral("title"),
                                        QStringLiteral("XuanHub")).toString();
    const QString msg   = payload.value(QStringLiteral("message")).toString();
    if (msg.isEmpty() || !m_tray || !QSystemTrayIcon::supportsMessages()) return;
    qDebug() << "[Tray] app.notify:" << title << "-" << msg;
    // 发布可能发生在 app.exec() 之前（插件加载阶段），顺延到事件循环里再弹
    QTimer::singleShot(0, m_tray, [tray = m_tray, title, msg] {
        tray->showMessage(title, msg, QSystemTrayIcon::Warning, 10000);
    });
}

void TrayIcon::onQuickMenuCommand(QVariantMap envelope) {
    const QVariantMap payload = envelope.value(QStringLiteral("data")).toMap();
    const QString action = payload.value(QStringLiteral("action")).toString();
    const bool enabled = payload.value(QStringLiteral("enabled")).toBool();

    if (action == QStringLiteral("settings")) {
        emit settingsRequested();
    } else if (action == QStringLiteral("autostart")) {
        setAutoStartEnabled(enabled);
    } else if (action == QStringLiteral("taskbarMode")) {
        setTaskbarMode(enabled);
    } else if (action == QStringLiteral("lightMode")) {
        if (m_theme) m_theme->switchMode(enabled ? QStringLiteral("light")
                                                 : QStringLiteral("dark"));
    } else if (action == QStringLiteral("reload")) {
        // forceReload 会卸载并重载所有插件。必须等当前插件的 QML→ViewModel→
        // EventBus 调用栈返回后再执行，否则会在插件代码仍在栈上时卸载 DLL。
        QTimer::singleShot(0, this, [this] { emit reloadRequested(); });
    } else if (action == QStringLiteral("quit")) {
        // 同样顺延到本次插件调用完全返回后再退出和析构插件。
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    } else {
        qWarning() << "[Tray] unknown quick menu action:" << action;
    }

    if (action != QStringLiteral("quit")) publishQuickMenuState();
}

void TrayIcon::publishQuickMenuState(QVariantMap) {
    if (!m_bus) return;

    // save() 会先更新 rawConfig，watcher 稍后才让 barMode() 生效；读 rawConfig
    // 能让插件中的开关在点击后立即显示已持久化的新状态。
    QString mode = m_config ? m_config->barMode() : QStringLiteral("appbar");
    if (m_config) {
        const QJsonObject bar = m_config->rawConfig().value(QStringLiteral("bar")).toObject();
        mode = bar.value(QStringLiteral("mode")).toString(mode);
    }

    QVariantMap state;
    state.insert(QStringLiteral("autoStartEnabled"), autostart::isEnabled());
    state.insert(QStringLiteral("taskbarMode"), mode == QStringLiteral("taskbar"));
    state.insert(QStringLiteral("themeMode"),
                 m_theme ? m_theme->mode() : QStringLiteral("dark"));
    m_bus->setState(QStringLiteral("app.quickMenu.state"), state);
}
