// PopupService.cpp — 实现与 DPI 无关的逻辑坐标定位及单活动弹窗策略
#include "PopupService.h"

#include <QtMath>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef Q_OS_WIN
#include <QtGui/qscreen_platform.h>
#include <windows.h>
#endif

namespace {
qreal number(const QVariantMap& map, const QString& key) {
    return map.value(key).toDouble();
}
}

PopupService::PopupService(QObject* parent) : QObject(parent) {}

QScreen* PopupService::screenForNativeWindow(QWindow* window) {
#ifdef Q_OS_WIN
    if (window && window->handle()) {
        const HMONITOR monitor = MonitorFromWindow(
            reinterpret_cast<HWND>(window->winId()), MONITOR_DEFAULTTONULL);
        if (monitor) {
            for (QScreen* candidate : QGuiApplication::screens()) {
                const auto* native = candidate->nativeInterface<QNativeInterface::QWindowsScreen>();
                if (native && native->handle() == monitor) return candidate;
            }
        }
    }
#endif
    return window && window->screen() ? window->screen() : QGuiApplication::primaryScreen();
}

PopupService::Placement PopupService::calculatePlacement(
    const QRectF& anchor, const QRectF& screen, const QSizeF& popupSize,
    qreal gap, const QString& barEdge) {
    Placement result;
    if (screen.width() <= 0 || screen.height() <= 0
        || popupSize.width() <= 0 || popupSize.height() <= 0) {
        result.topLeft = anchor.bottomLeft() + QPointF(0, qMax<qreal>(0, gap));
        return result;
    }

    const qreal safeGap = qMax<qreal>(0, gap);
    const qreal minX = screen.left();
    const qreal maxX = qMax(minX, screen.right() - popupSize.width());
    const qreal centeredX = anchor.center().x() - popupSize.width() / 2.0;
    const qreal x = qBound(minX, centeredX, maxX);

    const qreal belowY = anchor.bottom() + safeGap;
    const qreal aboveY = anchor.top() - safeGap - popupSize.height();
    const bool belowFits = belowY + popupSize.height() <= screen.bottom();
    const bool aboveFits = aboveY >= screen.top();
    const bool preferAbove = barEdge == QLatin1String("bottom");

    bool opensAbove = preferAbove;
    if (preferAbove && !aboveFits && belowFits) opensAbove = false;
    if (!preferAbove && !belowFits && aboveFits) opensAbove = true;
    if (!aboveFits && !belowFits) {
        const qreal roomAbove = anchor.top() - safeGap - screen.top();
        const qreal roomBelow = screen.bottom() - anchor.bottom() - safeGap;
        opensAbove = roomAbove > roomBelow;
    }

    const qreal rawY = opensAbove ? aboveY : belowY;
    const qreal maxY = qMax(screen.top(), screen.bottom() - popupSize.height());
    result.topLeft = QPointF(x, qBound(screen.top(), rawY, maxY));
    result.opensAbove = opensAbove;
    return result;
}

QVariantMap PopupService::calculate(const QVariantMap& anchor,
                                    const QVariantMap& screen,
                                    qreal popupWidth, qreal popupHeight,
                                    qreal gap, const QString& barEdge) const {
    const QRectF anchorRect(number(anchor, QStringLiteral("x")),
                            number(anchor, QStringLiteral("y")),
                            number(anchor, QStringLiteral("width")),
                            number(anchor, QStringLiteral("height")));
    const QRectF screenRect(number(screen, QStringLiteral("x")),
                            number(screen, QStringLiteral("y")),
                            number(screen, QStringLiteral("width")),
                            number(screen, QStringLiteral("height")));
    const Placement placement = calculatePlacement(
        anchorRect, screenRect, QSizeF(popupWidth, popupHeight), gap, barEdge);
    return {{QStringLiteral("x"), placement.topLeft.x()},
            {QStringLiteral("y"), placement.topLeft.y()},
            {QStringLiteral("opensAbove"), placement.opensAbove}};
}

void PopupService::activate(QObject* popup) {
    if (!popup || m_activePopup == popup) return;

    QObject* previous = m_activePopup.data();
    if (previous) previous->setProperty("visible", false);
    if (m_destroyedConnection) disconnect(m_destroyedConnection);

    m_activePopup = popup;
    m_destroyedConnection = connect(popup, &QObject::destroyed, this, [this] {
        // 该连接只绑定当前活动弹窗；切换时会先断开旧连接。QObject 发出
        // destroyed 前后 QPointer 的清空时序不应影响属性通知，因此直接清理。
        m_activePopup.clear();
        m_destroyedConnection = {};
        Q_EMIT activePopupChanged();
    });
    Q_EMIT activePopupChanged();
}

void PopupService::release(QObject* popup) {
    if (!popup || m_activePopup != popup) return;
    if (m_destroyedConnection) disconnect(m_destroyedConnection);
    m_destroyedConnection = {};
    m_activePopup.clear();
    Q_EMIT activePopupChanged();
}
