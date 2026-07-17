// BarWindow.cpp
#include "BarWindow.h"
#include "ConfigService.h"
#include "WorkAreaGuard.h"
#include "PluginListModel.h"
#include "ThemeManager.h"

#include <QScreen>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QDebug>

BarWindow::BarWindow(ConfigService* config, PluginListModel* model,
                       ThemeManager* theme, QWindow* parent)
    : QQuickView(parent), m_config(config), m_guard(new WorkAreaGuard(this)) {
    if (model) engine()->rootContext()->setContextProperty("pluginModel", model);
    if (theme) engine()->rootContext()->setContextProperty("themeManager", theme);

    setFlag(Qt::FramelessWindowHint);
    setFlag(Qt::WindowStaysOnTopHint);
    setFlag(Qt::Tool);  // 不进任务栏 / Alt+Tab
    // 点击 bar 不抢占当前应用焦点（WS_EX_NOACTIVATE 语义，鼠标事件照常投递）
    setFlag(Qt::WindowDoesNotAcceptFocus);

    setResizeMode(QQuickView::SizeRootObjectToView);
    setColor(QColor(0, 0, 0, 0));  // 透明，让 QML 自绘底色

    setSource(QUrl(QStringLiteral("qrc:/qt/qml/XuanHub/qml/main.qml")));
    if (status() != QQuickView::Ready) {
        qWarning() << "[BarWindow] QML not ready, status =" << status();
        for (const auto& e : errors()) qWarning() << "   " << e.toString();
    }
}

BarWindow::~BarWindow() {
    if (m_guard) m_guard->release();
}

void BarWindow::setupBar() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qWarning() << "[BarWindow] no primary screen";
        return;
    }

    const QRect sg = screen->geometry();   // 逻辑像素
    const int h = m_config->barHeight();   // 逻辑像素
    const bool top = (m_config->barEdge() != QStringLiteral("bottom"));
    const int y = top ? sg.y() : (sg.bottom() - h + 1);

    setGeometry(sg.x(), y, sg.width(), h);

    // reserve 工作区（物理像素）
    const qreal dpr = screen->devicePixelRatio();
    const int hPhys = qRound(h * dpr);
    if (top) {
        if (!m_guard->reserveTop(winId(), hPhys))
            qWarning() << "[BarWindow] appbar reserve failed (top)";
    }
    // TODO: bottom 边缘的 ABE_BOTTOM 支持（后续里程碑）
}
