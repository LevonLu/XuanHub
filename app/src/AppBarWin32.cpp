// AppBarWin32.cpp
#include "AppBarWin32.h"
#include <QDebug>
#include <QPointer>
#include <QAbstractNativeEventFilter>
#include <QAbstractEventDispatcher>
#include <QScopedValueRollback>

// 全局宏在 CMake 中定义：WIN32_LEAN_AND_MEAN / NOMINMAX / UNICODE
#include <windows.h>
#include <shellapi.h>   // SHAppBarMessage / APPBARDATA / ABE_*

namespace {
// appbar 回调消息需在 WM_APP(0x8000) .. 0xBFFF 范围内
constexpr UINT kAppBarCallbackMsg = WM_APP + 0x0001;
}

// 监听发到本窗口的 appbar 回调消息（只观察，不吞消息）
class AppBarWin32::Filter : public QAbstractNativeEventFilter {
public:
    explicit Filter(AppBarWin32* owner) : m_owner(owner) {}
    bool nativeEventFilter(const QByteArray&, void* msg, qintptr*) override {
        auto* m = static_cast<MSG*>(msg);
        if (m_owner && m->message == kAppBarCallbackMsg
            && reinterpret_cast<WId>(m->hwnd) == m_owner->m_hwnd) {
            m_owner->handleCallback(quintptr(m->wParam), qintptr(m->lParam));
        }
        return false;
    }
private:
    QPointer<AppBarWin32> m_owner;
};

AppBarWin32::AppBarWin32(QObject* parent) : QObject(parent) {}

AppBarWin32::~AppBarWin32() { unregister(); }

QRect AppBarWin32::lastRect() const {
    if (!m_registered) return {};
    // RECT 右/下边是排他的，直接换算为 QRect(x, y, w, h)
    return QRect(int(m_lastRc.left), int(m_lastRc.top),
                 int(m_lastRc.right - m_lastRc.left),
                 int(m_lastRc.bottom - m_lastRc.top));
}

bool AppBarWin32::registerEdge(WId hwnd, Edge edge, int heightPhysPx) {
    if (!hwnd || heightPhysPx <= 0) {
        qWarning() << "[AppBar] invalid hwnd or height";
        return false;
    }
    if (m_registered && m_hwnd == hwnd) {
        // 已注册：只更新参数并重新协商位置。绝不能走 ABM_REMOVE+ABM_NEW
        // 重注册——remove/new/setpos 会触发系统回发 ABN_POSCHANGED，
        // 与 needRelayout → setupBar → reserve 形成注册/注销死循环
        m_edge = edge;
        m_heightPhys = heightPhysPx;
        renegotiate();
        return true;
    }
    if (m_registered) unregister();   // hwnd 变了：完整注销重来

    m_hwnd = hwnd;
    m_edge = edge;
    m_heightPhys = heightPhysPx;

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(hwnd);

    // 防御崩溃残留：先尝试移除同窗口的旧注册（未注册时无害）
    SHAppBarMessage(ABM_REMOVE, &abd);

    // 注册新的 appbar，绑定回调消息
    abd.uCallbackMessage = kAppBarCallbackMsg;
    if (!SHAppBarMessage(ABM_NEW, &abd)) {
        qWarning() << "[AppBar] ABM_NEW failed (already registered?)";
        return false;
    }
    m_callbackMsg = kAppBarCallbackMsg;
    m_registered = true;
    m_lastRc = {};

    renegotiate();

    if (!m_filter) {
        m_filter = new Filter(this);
        QAbstractEventDispatcher::instance()->installNativeEventFilter(m_filter);
    }

    qDebug() << "[AppBar] registered"
             << (edge == Edge::Top ? "top" : "bottom")
             << ", heightPhys =" << heightPhysPx;
    return true;
}

void AppBarWin32::renegotiate() {
    if (!m_registered) return;

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(m_hwnd);
    abd.uEdge = (m_edge == Edge::Bottom) ? ABE_BOTTOM : ABE_TOP;
    abd.rc.left = 0;
    abd.rc.right = GetSystemMetrics(SM_CXSCREEN);   // 主屏物理宽（MVP 仅主屏）
    if (m_edge == Edge::Bottom) {
        abd.rc.bottom = GetSystemMetrics(SM_CYSCREEN);
        abd.rc.top = abd.rc.bottom - m_heightPhys;
    } else {
        abd.rc.top = 0;
        abd.rc.bottom = m_heightPhys;
    }

    SHAppBarMessage(ABM_QUERYPOS, &abd);  // 系统校正 rc（同边缘有任务栏/其他 appbar 时会被向内推）

    // 以系统给出的边缘坐标为准、厚度由我方决定（MSDN appbar 标准模式）：
    // 否则同边缘被占用时 QUERYPOS 会返回 top>bottom 的退化矩形
    if (m_edge == Edge::Bottom)
        abd.rc.top = abd.rc.bottom - m_heightPhys;
    else
        abd.rc.bottom = abd.rc.top + m_heightPhys;

    // 矩形未变化则跳过 SETPOS：避免与 ABN_POSCHANGED 通知互相触发成环
    if (abd.rc.left == m_lastRc.left && abd.rc.top == m_lastRc.top
        && abd.rc.right == m_lastRc.right && abd.rc.bottom == m_lastRc.bottom)
        return;
    m_lastRc = { abd.rc.left, abd.rc.top, abd.rc.right, abd.rc.bottom };

    SHAppBarMessage(ABM_SETPOS, &abd);    // 固定位置，系统据此 reserve 工作区
    qDebug() << "[AppBar] SETPOS rc =" << abd.rc.left << abd.rc.top
             << abd.rc.right << abd.rc.bottom;
}

void AppBarWin32::unregister() {
    if (m_filter) {
        if (auto* d = QAbstractEventDispatcher::instance())
            d->removeNativeEventFilter(m_filter);
        delete m_filter;
        m_filter = nullptr;
    }
    if (!m_registered) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(m_hwnd);
    abd.uCallbackMessage = m_callbackMsg;
    SHAppBarMessage(ABM_REMOVE, &abd);
    m_registered = false;
    m_lastRc = {};
    qDebug() << "[AppBar] unregistered";
}

void AppBarWin32::handleCallback(quintptr wParam, qintptr lParam) {
    if (m_inCallback) return;   // 防自身 SETPOS 引起的回声重入
    QScopedValueRollback<bool> guard(m_inCallback, true);

    switch (UINT(wParam)) {
    case ABN_POSCHANGED:
        // 任务栏/其他 appbar 变化（分辨率、DPI 变动也会伴随任务栏 resize）：
        // 交给窗口层重新布局并重新 reserve（内部有矩形变化检测，不会成环）
        emit needRelayout();
        break;
    case ABN_FULLSCREENAPP:
        // lParam=TRUE：全屏应用出现；FALSE：全屏退出
        emit fullscreenActiveChanged(lParam != 0);
        break;
    default:
        break;
    }
}
