// BrandIconRenderer.h — 按菜单几何比例生成多尺寸原生托盘图标
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// 仅绘制 hub / compact；原版与异常资源兜底继续由 TrayIcon 负责。
QIcon renderMenuVectorIcon(const QString& style, const QColor& color);
