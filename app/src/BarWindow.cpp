// BarWindow.cpp
#include "BarWindow.h"
#include "ConfigService.h"
#include "WorkAreaGuard.h"
#include "TaskbarEmbed.h"
#include "PluginListModel.h"
#include "ThemeManager.h"
#include "LocaleManager.h"
#include "PopupService.h"
#include "PluginActionRegistry.h"

#include <QScreen>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QMouseEvent>
#include <QTimer>
#include <QDebug>
#include <QEvent>

BarWindow::BarWindow(ConfigService* config, PluginListModel* model,
                       ThemeManager* theme, LocaleManager* locale,
                       PluginActionRegistry* pluginActions, QWindow* parent)
    : QQuickView(parent), m_config(config), m_guard(new WorkAreaGuard(this)),
      m_embed(new TaskbarEmbed(this)), m_popupService(new PopupService(this)) {
    if (model) engine()->rootContext()->setContextProperty("pluginModel", model);
    if (theme) engine()->rootContext()->setContextProperty("themeManager", theme);
    if (locale) {
        engine()->rootContext()->setContextProperty("i18n", locale);
        locale->registerEngine(engine());
    }
    // barConfig：bar.height/edge/theme/mode 供 QML（含插件 QML，同引擎根上下文）读取
    if (config) engine()->rootContext()->setContextProperty("barConfig", config);
    // barWindow：暴露本窗口给 QML（插件 QML 用 Connections 收听 barPressed 信号）
    engine()->rootContext()->setContextProperty("barWindow", this);
    // PopupService：插件共享定位与互斥服务。高级插件仍可继续自建 Window。
    engine()->rootContext()->setContextProperty("popupService", m_popupService);
    updatePopupScreenGeometry();
    // 主屏、工作区和缩放可在进程存活时改变；弹窗边界不能等下一次保存配置才刷新。
    const auto watchScreen = [this](QScreen* target) {
        connect(target, &QScreen::availableGeometryChanged,
                this, &BarWindow::updatePopupScreenGeometry);
    };
    for (QScreen* target : QGuiApplication::screens()) watchScreen(target);
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [this, watchScreen](QScreen* target) { watchScreen(target); updatePopupScreenGeometry(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &BarWindow::updatePopupScreenGeometry);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged,
            this, &BarWindow::updatePopupScreenGeometry);
    // 声明式动作注册表：插件菜单只拿到经过 Manifest 校验的稳定动作 ID。
    if (pluginActions)
        engine()->rootContext()->setContextProperty("pluginActions", pluginActions);

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
    // 逐像素透明：taskbar 模式靠它透出任务栏底色；appbar 模式 QML 自绘不透明
    // 底色，视觉无变化。QWindow 无 WA_TranslucentBackground，透明靠 alpha buffer +
    // 透明清屏色；必须在 create() 之前设置（此处构造期，main 里才 create）
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    setFormat(fmt);

    setResizeMode(QQuickView::SizeRootObjectToView);
    setColor(QColor(0, 0, 0, 0));  // 透明，让 QML 自绘底色

    setSource(QUrl(QStringLiteral("qrc:/qt/qml/XuanHub/qml/main.qml")));
    if (status() != QQuickView::Ready) {
        qWarning() << "[BarWindow] QML not ready, status =" << status();
        for (const auto& e : errors()) qWarning() << "   " << e.toString();
    }

    // taskbar 模式：QML 内容宽变化 → 重新定位（时钟数字变宽、插件增删都会变）
    if (QQuickItem* root = rootObject()) {
        m_embed->setContentWidth(qRound(root->implicitWidth()));
        connect(root, &QQuickItem::implicitWidthChanged, this, [this] {
            if (rootObject())
                m_embed->setContentWidth(qRound(rootObject()->implicitWidth()));
        });
    }
    // explorer 重启销毁任务栏（连同我们的子窗口）→ 重建窗口重新嵌入
    connect(m_embed, &TaskbarEmbed::taskbarVanished,
            this, &BarWindow::recreateForTaskbar);
}

BarWindow::~BarWindow() {
    if (m_guard) m_guard->release();
}

bool BarWindow::event(QEvent* event) {
    const bool refresh = event->type() == QEvent::Move
        || event->type() == QEvent::ScreenChangeInternal
        || event->type() == QEvent::DevicePixelRatioChange;
    const bool handled = QQuickView::event(event);
    if (refresh && m_embed) {
        // 外部任务栏的重定位/DPI 消息先交给 Qt，下一轮再读取最终原生屏幕。
        QTimer::singleShot(0, this, &BarWindow::updatePopupScreenGeometry);
    }
    return handled;
}

void BarWindow::updatePopupScreenGeometry() {
    // appbar 明确布局到主屏；taskbar 在 attach 后以 HWND 的实际监视器为准。
    // 不能只在 screen()==nullptr 时回退：外部父窗口下的非空关联也可能指向副屏。
    QScreen* target = m_embed && m_embed->isAttached()
        ? PopupService::screenForNativeWindow(this) : QGuiApplication::primaryScreen();
    if (target) {
        const QRect available = target->availableGeometry();
        if (engine()->rootContext()->contextProperty(QStringLiteral("barScreenGeometry")).toRect() != available)
            engine()->rootContext()->setContextProperty(QStringLiteral("barScreenGeometry"), available);
    }
}

void BarWindow::mousePressEvent(QMouseEvent* event) {
    // 先于 item 投递广播：Connections 直连接收，弹窗关闭发生在
    // 被点 item 的 onPressed/onClicked 之前（插件据此区分"点部件 toggle 关闭"
    // 与"点空白处仅关闭"）
    Q_EMIT barPressed();
    QQuickView::mousePressEvent(event);
}

void BarWindow::setupBar() {
    // taskbar 模式：嵌入原生任务栏左侧，不占工作区
    if (m_config->barMode() == QStringLiteral("taskbar")) {
        m_guard->release();   // 从 appbar 切来时归还已 reserve 的工作区（幂等）
        if (m_embed->attach(this, m_config->taskbarOffsetX())) {
            updatePopupScreenGeometry();
            return;
        }
        // 任务栏还没准备好（如 explorer 正在重启）→ 1s 后重试，
        // 直到 attach 成功或模式被热重载切走
        qWarning() << "[BarWindow] taskbar attach failed, retry in 1s";
        QTimer::singleShot(1000, this, [this] {
            if (m_config->barMode() == QStringLiteral("taskbar")) setupBar();
        });
        return;
    }
    if (m_embed->isAttached()) m_embed->detach();

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
        // 统一从物理矩形的 top/bottom 两条边各自换算再取差得高度：
        // 125%/175% 分数缩放下 round(h*dpr)（ABM 协商用）与
        // round(h/dpr) 两个方向取整不互逆，若再对 height 单独取整，
        // bar 底边与工作区边界会出现 1px 缝隙/压盖（实测）
        const int yLog = qRound(qreal(rcPhys.top()) / dpr);
        // 排他底边：QRect::bottom() 是含端点语义（top+height-1），直接用会少 1px
        const int bottomLog = qRound(qreal(rcPhys.top() + rcPhys.height()) / dpr);
        setGeometry(sg.x(), sg.y() + yLog,
                    sg.width(), bottomLog - yLog);
    } else {
        const int y = top ? sg.y() : (sg.bottom() - h + 1);
        setGeometry(sg.x(), y, sg.width(), h);
    }
    updatePopupScreenGeometry();
}

void BarWindow::recreateForTaskbar() {
    if (m_config->barMode() != QStringLiteral("taskbar")) return;
    qDebug() << "[BarWindow] recreate for taskbar (explorer restarted)";
    // 旧 HWND 已随任务栏销毁：先脱离（对失效句柄的 Win32 调用无害），
    // 再重建平台窗口并重新嵌入。destroy/create 保留 flags 与 QML 内容
    m_embed->detach();
    destroy();
    create();
    setupBar();
    show();
}
