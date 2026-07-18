// TrayIcon.cpp
#include "TrayIcon.h"
#include "ThemeManager.h"
#include "AutoStart.h"

#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSignalBlocker>

TrayIcon::TrayIcon(ThemeManager* theme, QObject* parent)
    : QObject(parent), m_theme(theme), m_menu(nullptr), m_autoStartAction(nullptr) {
    m_tray = new QSystemTrayIcon(this);
    refreshIcon();   // 首次绘制（读当前主题色板）
    m_tray->setToolTip(QStringLiteral("玄枢 XuanHub"));

    // 主题变化时重绘图标（ThemeManager 在主线程，AutoConnection 排队到 GUI 线程安全）
    if (m_theme) connect(m_theme, &ThemeManager::themeChanged, this, &TrayIcon::refreshIcon);

    m_menu = new QMenu();

    // 设置窗口（M8）
    QAction* settings = m_menu->addAction(QStringLiteral("Settings..."));
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);

    m_autoStartAction = m_menu->addAction(QStringLiteral("Auto Start"));
    m_autoStartAction->setCheckable(true);
    connect(m_autoStartAction, &QAction::toggled, this, &TrayIcon::setAutoStartEnabled);
    // 勾选状态与注册表现状对齐（阻断信号，避免初始化时回写一遍）；
    // 菜单每次弹出前再对齐一次 —— 设置窗口也可能改同一注册表键
    {
        QSignalBlocker blocker(m_autoStartAction);
        m_autoStartAction->setChecked(autostart::isEnabled());
    }
    connect(m_menu, &QMenu::aboutToShow, this, [this]() {
        QSignalBlocker blocker(m_autoStartAction);
        m_autoStartAction->setChecked(autostart::isEnabled());
    });

    QAction* toggleTheme = m_menu->addAction(QStringLiteral("Toggle Dark/Light"));
    connect(toggleTheme, &QAction::triggered, [this]() {
        // 仅切 light/dark 模式，保持当前主题包（switchMode 不写回文件，热重载时会回滚）
        const QString t = (m_theme->mode() == QStringLiteral("dark"))
                              ? QStringLiteral("light") : QStringLiteral("dark");
        m_theme->switchMode(t);
    });

    // 配置热重载（M7）：手动触发入口，与文件监听同一个 reload()
    QAction* reload = m_menu->addAction(QStringLiteral("Reload Config"));
    connect(reload, &QAction::triggered, this, &TrayIcon::reloadRequested);

    m_menu->addSeparator();
    QAction* quit = m_menu->addAction(QStringLiteral("Quit"));
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
