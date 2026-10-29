// TaskbarEmbedTest.cpp — 验证任务栏子窗口在原生几何变化后恢复并同步 QML 高度
#include "TaskbarEmbed.h"

#include <QAbstractNativeEventFilter>
#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

#include <windows.h>

class TaskbarEmbedTest final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
private:
    QQuickView m_window;
    TaskbarEmbed m_embed;
    HWND m_tray = nullptr;
    HWND m_hwnd = nullptr;
    int m_positionMessages = 0;
    // 测试子窗口放在任务栏客户区外，不遮挡用户正在使用的任务栏。
    static constexpr int Offset = -10000;
    static constexpr int ContentWidth = 161;

    QRect nativeGeometry() const {
        RECT rect{};
        if (!GetWindowRect(m_hwnd, &rect)) return {};
        POINT pos{rect.left, rect.top};
        if (!ScreenToClient(m_tray, &pos)) return {};
        return QRect(pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top);
    }

    QRect targetGeometry() const {
        RECT rect{};
        if (!GetClientRect(m_tray, &rect)) return {};
        const qreal dpr = m_window.devicePixelRatio();
        return QRect(rect.left + qRound(Offset * dpr), rect.top,
                     qRound(ContentWidth * dpr), rect.bottom - rect.top);
    }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->hwnd == m_hwnd && msg->message == WM_WINDOWPOSCHANGING)
            ++m_positionMessages;
        return false;
    }

private Q_SLOTS:
    void initTestCase() {
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("需要 Windows 原生窗口平台");
        m_tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!m_tray) QSKIP("当前会话没有原生任务栏");
        m_window.setFlags(Qt::FramelessWindowHint | Qt::Tool
                          | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        m_window.setResizeMode(QQuickView::SizeRootObjectToView);
        m_window.setColor(Qt::transparent);
        m_window.setContent(QUrl(), nullptr, new QQuickItem);
        qApp->installNativeEventFilter(this);
    }

    void init() {
        m_window.create();
        m_hwnd = reinterpret_cast<HWND>(m_window.winId());
        m_embed.setContentWidth(ContentWidth);
        QVERIFY(m_embed.attach(&m_window, Offset));
        QTRY_COMPARE(nativeGeometry(), targetGeometry());
        qInfo() << "测试窗口 DPR:" << m_window.devicePixelRatio();
    }

    void cleanup() {
        m_window.hide();
        m_embed.detach();
    }

    void cleanupTestCase() {
        qApp->removeNativeEventFilter(this);
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
            QCOMPARE(nativeGeometry(), expected);
        } else if (repairPath == QStringLiteral("dpi")) {
            QEvent dpiChange(QEvent::DevicePixelRatioChange);
            QCoreApplication::sendEvent(&m_window, &dpiChange);
            // 排空本轮事件即可恢复，不依赖下一次 500ms 轮询。
            QCoreApplication::processEvents();
            QCOMPARE(nativeGeometry(), expected);
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(nativeGeometry(), expected, 1500);
        }
        const int logicalHeight = qRound(expected.height() / m_window.devicePixelRatio());
        QTRY_COMPARE(m_window.height(), logicalHeight);
        QTRY_COMPARE(qRound(m_window.rootObject()->height()), logicalHeight);
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
