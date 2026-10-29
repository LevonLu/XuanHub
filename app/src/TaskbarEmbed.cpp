// TaskbarEmbed.cpp — 同步原生任务栏与 Qt 子窗口的实际几何和 DPI 变化
#include "TaskbarEmbed.h"

#include <QWindow>
#include <QDebug>
#include <QEvent>
#include <QRect>

#include <windows.h>

TaskbarEmbed::TaskbarEmbed(QObject* parent) : QObject(parent) {
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &TaskbarEmbed::poll);
}

TaskbarEmbed::~TaskbarEmbed() { delete m_foreign; }

bool TaskbarEmbed::attach(QWindow* win, int offsetXLogical) {
    if (!win) return false;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        qWarning() << "[TaskbarEmbed] Shell_TrayWnd not found";
        return false;
    }
    m_trayHwnd = tray;
    m_offsetX = offsetXLogical;

    // 已 attach 同一窗口：幂等更新（热重载反复走到），不重复 SetParent
    if (m_win != win) {
        // QWindow::fromWinId 包裹任务栏为 Qt 父窗口（Qt 官方支持的嵌入外部原生窗口方式）。
        // 每次调用都新建包装对象，缓存为成员、替换时删旧的（否则 explorer 每次
        // 重启都泄漏一个）；失败必须直接返回——不返回的话窗口会被改成无父
        // WS_CHILD 从桌面静默消失（由 BarWindow 的 1s 重试兜底）
        QWindow* foreign = QWindow::fromWinId(reinterpret_cast<WId>(tray));
        if (!foreign) {
            qWarning() << "[TaskbarEmbed] QWindow::fromWinId failed, will retry";
            return false;
        }
        if (m_win) m_win->removeEventFilter(this);
        m_win = win;
        win->installEventFilter(this);
        delete m_foreign;
        m_foreign = foreign;
        win->setParent(m_foreign);
        // 兜底确保 Win32 样式是 WS_CHILD：Qt 对外挂父窗口的样式转换未必可靠
        const HWND hwnd = reinterpret_cast<HWND>(win->winId());
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        // SWP_FRAMECHANGED 让样式修改立即生效；WS_EX_NOACTIVATE（不抢焦点）保持不动
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        qDebug() << "[TaskbarEmbed] attached to Shell_TrayWnd";
    }

    m_timer.start();
    relayout();
    return true;
}

void TaskbarEmbed::detach() {
    m_timer.stop();
    if (m_win) {
        QWindow* w = m_win;
        w->removeEventFilter(this);
        m_win.clear();
        const HWND hwnd = reinterpret_cast<HWND>(w->winId());
        // 句柄可能已随任务栏销毁（explorer 重启），Win32 调用对失效句柄只是失败返回，无害
        w->setParent(nullptr);
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style = (style & ~WS_CHILD) | WS_POPUP;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        qDebug() << "[TaskbarEmbed] detached";
    }
    m_trayHwnd = nullptr;
}

bool TaskbarEmbed::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_win && event->type() == QEvent::DevicePixelRatioChange
            && !m_relayoutPending) {
        // Qt 处理 DPI 通知时还会调整原生子窗口；等本轮消息完成后再按任务栏布局，
        // 保留 Qt 自身对 DPR、渲染目标和 QML 逻辑尺寸的更新。
        m_relayoutPending = true;
        QTimer::singleShot(0, this, [this] {
            m_relayoutPending = false;
            relayout();
        });
    }
    return QObject::eventFilter(watched, event);
}

void TaskbarEmbed::setContentWidth(int wLogical) {
    if (wLogical == m_contentWidth) return;
    m_contentWidth = wLogical;
    if (isAttached()) relayout();
}

void TaskbarEmbed::relayout() {
    if (m_win.isNull() || !m_trayHwnd) return;
    HWND tray = static_cast<HWND>(m_trayHwnd);
    if (!IsWindow(tray)) {
        emit taskbarVanished();
        return;
    }
    RECT rc{};
    if (!GetClientRect(tray, &rc)) return;
    const int trayH = rc.bottom - rc.top;
    if (trayH <= 0) return;

    // QML 内容按逻辑像素，Win32 矩形按物理像素，用窗口 DPR 换算
    const qreal dpr = m_win->devicePixelRatio();
    const int w = qMax(1, qRound(m_contentWidth * dpr));
    // 子窗口直接使用父窗口客户区坐标，不把任务栏边框算进可用高度。
    const QPoint pos(rc.left + qRound(m_offsetX * dpr), rc.top);

    const HWND hwnd = reinterpret_cast<HWND>(m_win->winId());
    const QRect target(pos, QSize(w, trayH));
    RECT actual{};
    if (!GetWindowRect(hwnd, &actual)) return;
    POINT actualPos{actual.left, actual.top};
    if (!ScreenToClient(tray, &actualPos)) return;
    const QRect current(actualPos.x, actualPos.y,
                        actual.right - actual.left, actual.bottom - actual.top);
    // DPI 变化期间 Qt/Windows 会再次缩放子窗口；即使期望矩形没变，实际矩形也可能
    // 已被改小。核对 HWND 当前几何，不能用“上次请求成功”代替当前状态。
    if (target == current) return;
    // 几何一致时仍不重复 SetWindowPos，保留稳定状态下避免无谓窗口消息的优化。
    if (!SetWindowPos(hwnd, HWND_TOP, pos.x(), pos.y(), w, trayH,
                      SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        qWarning() << "[TaskbarEmbed] SetWindowPos failed:" << GetLastError();
    }
}

void TaskbarEmbed::poll() {
    if (m_win.isNull()) { m_timer.stop(); return; }
    HWND tray = static_cast<HWND>(m_trayHwnd);
    if (!tray || !IsWindow(tray)) {
        // explorer 重启：父窗口销毁时子窗口一并销毁，我们的 HWND 已失效，
        // 停轮询并上报，由上层重建窗口后重新 attach
        m_timer.stop();
        emit taskbarVanished();
        return;
    }
    relayout();
}
