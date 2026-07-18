// SettingsController.cpp
#include "SettingsController.h"
#include "SettingsViewModel.h"
#include "ThemeManager.h"

#include <QDebug>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickStyle>

SettingsController::SettingsController(ConfigService* config, ThemeManager* theme,
                                       ThemeRegistry* registry,
                                       const QString& exeDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_registry(registry),
      m_exeDir(exeDir) {}

SettingsController::~SettingsController() {
    delete m_window.data();   // QWindow 无 QObject 父对象，手动释放
}

void SettingsController::open() {
    if (!m_window) {
        // Basic 样式：自绘色板随主题（进程内仅设置窗口用 Controls，不影响 bar 的 QML）
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        m_vm = new SettingsViewModel(m_config, m_registry, m_exeDir, this);
        auto* w = new QQuickView();
        w->setTitle(QStringLiteral("玄枢设置 XuanHub Settings"));
        // 无边框：去掉系统标题栏，改由 QML 自绘标题栏（配色跟随主题）。
        // 系统 Windows 标题栏不受 Qt 主题控制，无法改色，故用 Frameless + 自绘方案。
        w->setFlag(Qt::FramelessWindowHint);
        // 透明窗口背景：让 QML 圆角外的区域能透出桌面（隐式启用 WA_TranslucentBackground）。
        // 代价：失去系统阴影，改由 QML DropShadow 补偿。
        w->setColor(QColor(Qt::transparent));
        w->setResizeMode(QQuickView::SizeRootObjectToView);
        w->resize(560, 640);
        w->rootContext()->setContextProperty(QStringLiteral("settingsVM"), m_vm);
        w->rootContext()->setContextProperty(QStringLiteral("themeManager"), m_theme);
        w->setSource(QUrl(QStringLiteral("qrc:/qt/qml/XuanHub/qml/SettingsWindow.qml")));
        if (w->status() != QQuickView::Ready) {
            for (const auto& e : w->errors())
                qWarning() << "[Settings] QML error:" << e.toString();
        }
        // 标题栏关闭按钮 = 隐藏（QWindow::close 默认行为即 hide），窗口不销毁
        m_window = w;
    }
    m_vm->reloadFromConfig();
    m_vm->reloadThemePacks();   // 打开时刷新主题包下拉（用户可能新装/卸载了包）
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}
