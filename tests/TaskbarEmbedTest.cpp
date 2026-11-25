// TaskbarEmbedTest.cpp — 验证任务栏子窗口避让开始按钮、恢复原生几何并同步 QML 高度
#include "TaskbarEmbed.h"
#include "PopupService.h"

#include <QAbstractNativeEventFilter>
#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <QtMath>
#include <QtTest>

#include <windows.h>
#include <memory>

class TaskbarEmbedTest final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
private:
    QQuickView m_window;
    TaskbarEmbed m_embed;
    HWND m_tray = nullptr;
    HWND m_hwnd = nullptr;
    int m_positionMessages = 0;
    // 保持 HWND 位于真实任务栏屏幕内，避免远离屏幕的坐标让 Qt 关联到另一块屏幕；
    // 窗口完全透明且不接收输入，不遮挡用户正在使用的任务栏。
    int m_offset = 0;
    static constexpr int ContentWidth = 161;

    QRect inTray(HWND window) const {
        RECT rect{};
        if (!window || !GetWindowRect(window, &rect)) return {};
        POINT pos{rect.left, rect.top};
        if (!ScreenToClient(m_tray, &pos)) return {};
        return QRect(pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top);
    }

    QRect nativeGeometry() const { return inTray(m_hwnd); }

    QRect trayGeometry() const {
        RECT rect{};
        if (!GetClientRect(m_tray, &rect)) return {};
        return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }

    QRect startButton() const {
        return inTray(FindWindowExW(m_tray, nullptr, L"Start", nullptr));
    }

    QRect targetGeometry() const {
        const qreal dpr = m_window.devicePixelRatio();
        return taskbar_layout::viewport(trayGeometry(), startButton(), qRound(m_offset * dpr),
                                        qRound(ContentWidth * dpr));
    }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->hwnd == m_hwnd && msg->message == WM_WINDOWPOSCHANGING)
            ++m_positionMessages;
        return false;
    }

private Q_SLOTS:
    void initTestCase() {
        m_window.setFlags(Qt::FramelessWindowHint | Qt::Tool
                          | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        m_window.setResizeMode(QQuickView::SizeRootObjectToView);
        m_window.setColor(Qt::transparent);
        m_window.setOpacity(0);
        m_window.setContent(QUrl(), nullptr, new QQuickItem);
        qApp->installNativeEventFilter(this);
    }

    void init() {
        // 纯几何用例也在没有 Explorer 的 Windows 测试会话里执行。
        if (qstrcmp(QTest::currentTestFunction(), "viewportReservesStartButton") == 0) return;
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("需要 Windows 原生窗口平台");
        m_tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!m_tray) QSKIP("当前会话没有原生任务栏");
        const QRect start = startButton();
        if (!start.intersects(trayGeometry())) QSKIP("当前任务栏没有可测的原生开始按钮");
        m_window.create();
        m_hwnd = reinterpret_cast<HWND>(m_window.winId());
        // 开始按钮左对齐时，DPI 恢复用例使用按钮右侧的显式偏移，保持窗口位于屏内。
        m_offset = start.left() > 1 ? 0 : qCeil((start.right() + 2) / m_window.devicePixelRatio());
        m_embed.setContentWidth(ContentWidth);
        QVERIFY(m_embed.attach(&m_window, m_offset));
        QTRY_COMPARE(nativeGeometry(), targetGeometry());
        qInfo() << "测试窗口 DPR:" << m_window.devicePixelRatio();
    }

    void cleanup() {
        if (!m_embed.isAttached()) return;
        m_window.hide();
        m_embed.detach();
    }

    void cleanupTestCase() {
        qApp->removeNativeEventFilter(this);
    }

    void viewportReservesStartButton_data() {
        QTest::addColumn<QRect>("start");
        QTest::addColumn<int>("offset");
        QTest::addColumn<int>("width");
        QTest::addColumn<QRect>("expected");
        const QRect center(400, 4, 48, 40);
        QTest::newRow("short") << center << 8 << 90 << QRect(8, 0, 90, 48);
        QTest::newRow("touch-left-padding") << center << 380 << 60 << QRect(380, 0, 19, 48);
        QTest::newRow("negative-offset") << center << -20 << 600 << QRect(-20, 0, 419, 48);
        QTest::newRow("fully-off-left") << center << -100 << 100 << QRect();
        QTest::newRow("offset-on-start") << center << 415 << 80 << QRect(0, 0, 80, 48);
        QTest::newRow("explicit-right") << center << 449 << 700 << QRect(449, 0, 551, 48);
        QTest::newRow("beyond-tray") << center << 1000 << 1 << QRect();
        QTest::newRow("no-content") << center << 0 << 0 << QRect();
        QTest::newRow("no-left-space") << QRect(0, 4, 48, 40) << 0 << 100 << QRect();
        QTest::newRow("left-aligned-explicit-right") << QRect(0, 4, 48, 40) << 49 << 100
                                                     << QRect(49, 0, 100, 48);
        QTest::newRow("missing-start") << QRect() << 0 << 100 << QRect();
        QTest::newRow("zero-width-start") << QRect(400, 4, 0, 40) << 0 << 100 << QRect();
        QTest::newRow("start-outside-tray") << QRect(400, 50, 48, 40) << 0 << 100 << QRect();
    }

    void viewportReservesStartButton() {
        QFETCH(QRect, start);
        QFETCH(int, offset);
        QFETCH(int, width);
        QFETCH(QRect, expected);
        QCOMPARE(taskbar_layout::viewport(QRect(0, 0, 1000, 48), start, offset, width), expected);
    }

    void avoidsStartWhenContentGrows() {
        const QRect tray = trayGeometry();
        const QRect start = startButton();
        const qreal dpr = m_window.devicePixelRatio();
        m_embed.setContentWidth(qCeil(tray.width() / dpr) + 1);
        QVERIFY(m_embed.attach(&m_window, 0));
        // 直接核对两个原生 HWND 的物理坐标，覆盖自然宽度越过开始按钮的实际场景。
        const int leftWidth = start.left() - tray.left() - 1;
        if (leftWidth > 0) {
            QTRY_COMPARE(nativeGeometry(), QRect(tray.topLeft(), QSize(leftWidth, tray.height())));
        } else {
            QTRY_VERIFY(!nativeGeometry().intersects(tray));
            QVERIFY(m_embed.attach(&m_window, m_offset));
        }
        QVERIFY(!nativeGeometry().intersects(start));
        m_embed.setContentWidth(1);
        const int room = leftWidth > 0 ? leftWidth : tray.width() - qRound(m_offset * dpr);
        QTRY_COMPARE(nativeGeometry().width(), qMin(room, qRound(dpr)));
        QVERIFY(nativeGeometry().intersects(tray));
        QVERIFY(!nativeGeometry().intersects(start));
    }

    void restoresChangedGeometry_data() {
        QTest::addColumn<QString>("repairPath");
        QTest::newRow("explicit-relayout") << QStringLiteral("direct");
        QTest::newRow("poll-after-native-resize") << QStringLiteral("poll");
        QTest::newRow("queued-dpi-change") << QStringLiteral("dpi");
    }

    void restoresChangedGeometry() {
        QFETCH(QString, repairPath);
        const QRect expected = targetGeometry();
        // 模拟 Qt/Windows 在我们布局后再次缩放/移动 HWND，内容宽度及目标均未变化。
        QVERIFY(SetWindowPos(m_hwnd, nullptr, expected.x() + 7, expected.y() + 3,
                             expected.width() / 2, expected.height() / 2,
                             SWP_NOZORDER | SWP_NOACTIVATE));
        QVERIFY(nativeGeometry() != expected);
        if (repairPath == QStringLiteral("direct")) {
            m_embed.relayout();
            QCOMPARE(nativeGeometry(), targetGeometry());
        } else if (repairPath == QStringLiteral("dpi")) {
            QEvent dpiChange(QEvent::DevicePixelRatioChange);
            QCoreApplication::sendEvent(&m_window, &dpiChange);
            // 排空本轮事件即可恢复，不依赖下一次 500ms 轮询。
            QCoreApplication::processEvents();
            QCOMPARE(nativeGeometry(), targetGeometry());
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(nativeGeometry(), targetGeometry(), 1500);
        }
        // 原生消息可能把初始屏幕 DPR 切换为任务栏 DPR；重试时必须按当前值计算，
        // 不能跨越事件处理仍固定使用切换前的期望矩形或逻辑高度。
        QTRY_COMPARE(m_window.height(),
                     qRound(targetGeometry().height() / m_window.devicePixelRatio()));
        QTRY_COMPARE(qRound(m_window.rootObject()->height()), m_window.height());
    }

    void stableGeometryDoesNotRepeatSetWindowPos() {
        QCoreApplication::processEvents();
        m_positionMessages = 0;
        m_embed.relayout();
        m_embed.relayout();
        QTest::qWait(1100);
        QCOMPARE(nativeGeometry(), targetGeometry());
        QCOMPARE(m_positionMessages, 0);
    }

    void popupScreenMatchesNativeTaskbar() {
        const HMONITOR expected = MonitorFromWindow(m_tray, MONITOR_DEFAULTTONULL);
        QVERIFY(expected);
        QScreen* actual = PopupService::screenForNativeWindow(&m_window);
        QVERIFY(actual);
        const auto* native = actual->nativeInterface<QNativeInterface::QWindowsScreen>();
        QVERIFY(native);
        QCOMPARE(native->handle(), expected);
        qInfo() << "原生任务栏屏幕:" << actual->name() << actual->availableGeometry()
                << "Qt 窗口关联:" << m_window.screen()->name();
    }

    void foreignWindowScreenUsesNativeMonitor() {
        QScreen* target = nullptr;
        for (QScreen* candidate : QGuiApplication::screens())
            if (candidate != QGuiApplication::primaryScreen()) { target = candidate; break; }
        if (!target) QSKIP("需要两块显示器");
        const auto* native = target->nativeInterface<QNativeInterface::QWindowsScreen>();
        QVERIFY(native);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        QVERIFY(GetMonitorInfoW(native->handle(), &info));
        HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
                                    WS_POPUP, info.rcMonitor.left + 40, info.rcMonitor.top + 40,
                                    80, 40, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        QVERIFY(hwnd);
        {
            std::unique_ptr<QWindow> foreign(QWindow::fromWinId(reinterpret_cast<WId>(hwnd)));
            QVERIFY(foreign);
            qInfo() << "外部窗口 Qt 关联:" << foreign->screen()->name()
                    << "原生屏幕:" << target->name();
            QCOMPARE(PopupService::screenForNativeWindow(foreign.get()), target);
        }
        QVERIFY(DestroyWindow(hwnd));
    }

    void queuedDpiChangeAfterDetachIsHarmless() {
        QEvent dpiChange(QEvent::DevicePixelRatioChange);
        QCoreApplication::sendEvent(&m_window, &dpiChange);
        m_window.hide();
        m_embed.detach();
        const QRect detached = m_window.geometry();
        QCoreApplication::processEvents();
        QVERIFY(!m_embed.isAttached());
        QVERIFY(!GetParent(m_hwnd));
        QCOMPARE(m_window.geometry(), detached);
    }
};

int main(int argc, char** argv) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    TaskbarEmbedTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TaskbarEmbedTest.moc"
