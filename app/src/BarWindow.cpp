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
    // barConfig：bar.height/edge/theme 供 QML（含插件 QML，同引擎根上下文）读取
    if (config) engine()->rootContext()->setContextProperty("barConfig", config);

    // 工作区参数变化（ABN_POSCHANGED）→ 重新布局并重新 reserve
    connect(m_guard, &WorkAreaGuard::needRelayout,
            this, &BarWindow::setupBar);
    // 全屏应用出现（ABN_FULLSCREENAPP）→ bar 隐藏让层；退出 → 恢复。
    // 注意：lambda 不支持 Qt::UniqueConnection，必须且只能在构造函数里连一次
    connect(m_guard, &WorkAreaGuard::fullscreenActiveChanged, this,
            [this](bool active) { if (active) hide(); else show(); });

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

    // reserve 工作区（物理像素）
    const qreal dpr = screen->devicePixelRatio();
    const int hPhys = qRound(h * dpr);
    const auto edge = top ? WorkAreaGuard::Edge::Top : WorkAreaGuard::Edge::Bottom;
    if (!m_guard->reserve(winId(), edge, hPhys)) {
        qWarning() << "[BarWindow] appbar reserve failed"
                   << (top ? "(top)" : "(bottom)");
        return;
    }

    // 以系统协商后的矩形定位窗口（物理→逻辑换算）：
    // 同边缘被任务栏/其他 appbar 占用时 bar 会被向内推移，
    // 不能只用"屏幕边缘 - 高度"的理想位置，否则窗口会压在任务栏上
    const QRect rcPhys = m_guard->lastRect();
    if (rcPhys.height() > 0) {
        setGeometry(sg.x(), sg.y() + qRound(qreal(rcPhys.top()) / dpr),
                    sg.width(), qRound(qreal(rcPhys.height()) / dpr));
    } else {
        const int y = top ? sg.y() : (sg.bottom() - h + 1);
        setGeometry(sg.x(), y, sg.width(), h);
    }
}
