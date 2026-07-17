// SettingsController.cpp
#include "SettingsController.h"
#include "SettingsViewModel.h"
#include "ThemeManager.h"

#include <QDebug>
#include <QQuickView>
#include <QQmlContext>
#include <QQuickStyle>

SettingsController::SettingsController(ConfigService* config, ThemeManager* theme,
                                       const QString& exeDir, QObject* parent)
    : QObject(parent), m_config(config), m_theme(theme), m_exeDir(exeDir) {}

SettingsController::~SettingsController() {
    delete m_window.data();   // QWindow 无 QObject 父对象，手动释放
}

void SettingsController::open() {
    if (!m_window) {
        // Basic 样式：自绘色板随主题（进程内仅设置窗口用 Controls，不影响 bar 的 QML）
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        m_vm = new SettingsViewModel(m_config, m_exeDir, this);
        auto* w = new QQuickView();
        w->setTitle(QStringLiteral("玄枢设置 XuanHub Settings"));
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
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}
