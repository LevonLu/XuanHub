// BrandIconRenderer.cpp — 以 24×24 网格绘制方框枢纽与紧凑 H
#include "BrandIconRenderer.h"

#include <QPainter>
#include <QPixmap>

QIcon renderMenuVectorIcon(const QString& style, const QColor& color) {
    const bool hub = style == QStringLiteral("hub");
    if (!hub && style != QStringLiteral("compact")) return {};

    QIcon icon;
    // Windows 托盘会随 DPI 请求不同尺寸，逐尺寸直接绘制，不放大低分辨率位图。
    // 几何规则与 app/qml/BrandIcon.qml、plugins/xuanhub_menu/ICON.md 保持一致。
    for (const int size : {16, 20, 24, 32, 40, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        if (hub) {
            line(9, 9, 4, 4);
            line(15, 9, 20, 4);
            line(9, 15, 4, 20);
            line(15, 15, 20, 20);
            // QPainter 描边以路径为中心；内缩 1，使方框外边界仍为 (8,8)–(16,16)。
            painter.drawRoundedRect(QRectF(9, 9, 6, 6), 0.5, 0.5);
        } else {
            line(8, 8, 8, 16);
            line(16, 8, 16, 16);
            line(8, 12, 16, 12);
            line(8, 8, 4, 4);
            line(16, 8, 20, 4);
            line(8, 16, 4, 20);
            line(16, 16, 20, 20);
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
