// TrayIcon.cpp
#include "TrayIcon.h"
#include "ThemeManager.h"

#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QAction>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>

TrayIcon::TrayIcon(ThemeManager* theme, QObject* parent)
    : QObject(parent), m_theme(theme), m_menu(nullptr), m_autoStartAction(nullptr) {
    m_tray = new QSystemTrayIcon(this);
    // 绘制简单托盘图标（避免 QApplication::style() 依赖）
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    {
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#1e1e2e"));
        p.setPen(QPen(QColor("#89b4fa"), 2));
        p.drawRoundedRect(4, 4, 24, 24, 6, 6);
        p.setPen(QPen(QColor("#cdd6f4"), 2));
        p.drawText(QRect(4, 4, 24, 24), Qt::AlignCenter, QStringLiteral("XH"));
    }
    m_tray->setIcon(QIcon(px));
    m_tray->setToolTip(QStringLiteral("玄枢 XuanHub"));

    m_menu = new QMenu();

    m_autoStartAction = m_menu->addAction(QStringLiteral("Auto Start"));
    m_autoStartAction->setCheckable(true);
    connect(m_autoStartAction, &QAction::toggled, this, &TrayIcon::setAutoStartEnabled);

    QAction* toggleTheme = m_menu->addAction(QStringLiteral("Toggle Theme"));
    connect(toggleTheme, &QAction::triggered, [this]() {
        const QString t = (m_theme->currentTheme() == QStringLiteral("dark"))
                              ? QStringLiteral("light") : QStringLiteral("dark");
        m_theme->switchTheme(t);
    });

    m_menu->addSeparator();
    QAction* quit = m_menu->addAction(QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, []() { QCoreApplication::quit(); });

    m_tray->setContextMenu(m_menu);
}

void TrayIcon::show() { m_tray->show(); }

void TrayIcon::recreate() {
    // Explorer 重启后托盘图标可能丢失，先隐藏再显示重建
    m_tray->hide();
    m_tray->show();
}

void TrayIcon::setAutoStartEnabled(bool on) {
    QSettings s(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                QSettings::NativeFormat);
    if (on)
        s.setValue(QStringLiteral("XuanHub"),
                   QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    else
        s.remove(QStringLiteral("XuanHub"));
}
