// WorkAreaGuard.cpp
#include "WorkAreaGuard.h"
#include "AppBarWin32.h"

WorkAreaGuard::WorkAreaGuard(QObject* parent)
    : QObject(parent), m_appbar(new AppBarWin32(this)) {
    connect(m_appbar, &AppBarWin32::needRelayout,
            this, &WorkAreaGuard::needRelayout);
    connect(m_appbar, &AppBarWin32::fullscreenActiveChanged,
            this, &WorkAreaGuard::fullscreenActiveChanged);
}

WorkAreaGuard::~WorkAreaGuard() { release(); }

bool WorkAreaGuard::reserve(WId hwnd, Edge edge, int heightPhysPx) {
    return m_appbar->registerEdge(hwnd,
        edge == Edge::Bottom ? AppBarWin32::Edge::Bottom : AppBarWin32::Edge::Top,
        heightPhysPx);
}

QRect WorkAreaGuard::lastRect() const {
    return m_appbar->lastRect();
}

void WorkAreaGuard::release() {
    if (m_appbar) m_appbar->unregister();
}
