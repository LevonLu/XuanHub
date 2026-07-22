// TrayIcon.cpp
#include "TrayIcon.h"
#include "ThemeManager.h"
#include "ConfigService.h"
#include "AutoStart.h"
#include "barplugin/EventBus.h"

#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSignalBlocker>
#include <QTimer>

TrayIcon::TrayIcon(ThemeManager* theme, xuanhub::EventBus* bus,
                   ConfigService* config, QObject* parent)
    : QObject(parent), m_theme(theme), m_config(config),
      m_menu(nullptr), m_autoStartAction(nullptr), m_taskbarModeAction(nullptr) {
    m_tray = new QSystemTrayIcon(this);
    refreshIcon();   // 首次绘制（读当前主题色板）
    m_tray->setToolTip(QStringLiteral("玄枢 XuanHub"));

    // 主题变化时重绘图标（ThemeManager 在主线程，AutoConnection 排队到 GUI 线程安全）
    if (m_theme) connect(m_theme, &ThemeManager::themeChanged, this, &TrayIcon::refreshIcon);

    // 插件经 EventBus 请求的气泡提示（如 pybridge 未检测到 Python 环境）。
    // 注意 slot 用裸签名串而非 SLOT() 宏：EventBus 经 QMetaObject::invokeMethod
    // 投递，后者不接受 SLOT() 的 "1" 前缀（会报 No such method）。
    if (bus) bus->subscribe(QStringLiteral("app.notify"), this, "onAppNotify(QVariantMap)");

    m_menu = new QMenu();

    // 设置窗口（M8）
    QAction* settings = m_menu->addAction(QStringLiteral("设置..."));
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);

    m_autoStartAction = m_menu->addAction(QStringLiteral("开机自启"));
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
    m_taskbarModeAction = m_menu->addAction(QStringLiteral("任务栏模式"));
    m_taskbarModeAction->setCheckable(true);
    connect(m_taskbarModeAction, &QAction::toggled, this, &TrayIcon::setTaskbarMode);
    if (m_config) {
        QSignalBlocker blocker(m_taskbarModeAction);
        m_taskbarModeAction->setChecked(m_config->barMode() == QStringLiteral("taskbar"));
    }

    QAction* toggleTheme = m_menu->addAction(QStringLiteral("切换深色/浅色"));
    connect(toggleTheme, &QAction::triggered, [this]() {
        // 仅切 light/dark 模式，保持当前主题包（switchMode 不写回文件，热重载时会回滚）
        const QString t = (m_theme->mode() == QStringLiteral("dark"))
                              ? QStringLiteral("light") : QStringLiteral("dark");
        m_theme->switchMode(t);
    });

    // 配置热重载（M7）：手动触发入口，与文件监听同一个 reload()
    QAction* reload = m_menu->addAction(QStringLiteral("重新加载配置"));
    connect(reload, &QAction::triggered, this, &TrayIcon::reloadRequested);

    m_menu->addSeparator();
    QAction* quit = m_menu->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, []() { QCoreApplication::quit(); });

    m_tray->setContextMenu(m_menu);
}

void TrayIcon::show() { m_tray->show(); }

void TrayIcon::refreshIcon() {
    // 从当前主题色板取色绘制托盘图标（底色=bg，边框=accent，文字=fg）
    // 主题不存在或缺字段时给深色兜底
    QColor bg     = QColor(QStringLiteral("#1e1e2e"));
    QColor accent = QColor(QStringLiteral("#89b4fa"));
    QColor fg     = QColor(QStringLiteral("#cdd6f4"));
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
