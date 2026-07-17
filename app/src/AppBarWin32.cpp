// AppBarWin32.cpp
#include "AppBarWin32.h"
#include <QDebug>

// 全局宏在 CMake 中定义：WIN32_LEAN_AND_MEAN / NOMINMAX / UNICODE
#include <windows.h>
#include <shellapi.h>   // SHAppBarMessage / APPBARDATA / ABE_*

namespace {
// appbar 回调消息需在 WM_APP(0x8000) .. 0xBFFF 范围内
constexpr UINT kAppBarCallbackMsg = WM_APP + 0x0001;
}

AppBarWin32::AppBarWin32(QObject* parent) : QObject(parent) {}

AppBarWin32::~AppBarWin32() { unregister(); }

bool AppBarWin32::registerTop(WId hwnd, int heightPhysPx) {
    if (!hwnd || heightPhysPx <= 0) {
        qWarning() << "[AppBar] invalid hwnd or height";
        return false;
    }
    m_hwnd = hwnd;

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(hwnd);

    // 防御崩溃残留：先尝试移除同窗口的旧注册（未注册时无害）
    SHAppBarMessage(ABM_REMOVE, &abd);

    // 注册新的 appbar，绑定回调消息
    abd.uCallbackMessage = kAppBarCallbackMsg;
    const BOOL ok = SHAppBarMessage(ABM_NEW, &abd);
    if (!ok) {
        qWarning() << "[AppBar] ABM_NEW failed (already registered?)";
        return false;
    }
    m_callbackMsg = kAppBarCallbackMsg;

    // 协商并固定位置：顶部 ABE_TOP
    abd.uEdge = ABE_TOP;
    abd.rc.left = 0;
    abd.rc.top = 0;
    abd.rc.right = GetSystemMetrics(SM_CXSCREEN);   // 主屏物理宽
    abd.rc.bottom = heightPhysPx;

    SHAppBarMessage(ABM_QUERYPOS, &abd);  // 系统校正 rc
    SHAppBarMessage(ABM_SETPOS, &abd);    // 固定位置，系统据此 reserve 工作区

    m_registered = true;
    qDebug() << "[AppBar] registered top, heightPhys =" << heightPhysPx
             << "rc.bottom =" << abd.rc.bottom;
    return true;
}

void AppBarWin32::unregister() {
    if (!m_registered) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = reinterpret_cast<HWND>(m_hwnd);
    abd.uCallbackMessage = m_callbackMsg;
    SHAppBarMessage(ABM_REMOVE, &abd);
    m_registered = false;
    qDebug() << "[AppBar] unregistered";
}

// TODO(M2/M5): 通过 QAbstractNativeEventFilter 处理 m_callbackMsg 对应的
// ABN_FULLSCREENAPP / ABN_POSCHANGED 等通知，以便在全屏应用/分辨率变化时重协商位置。
