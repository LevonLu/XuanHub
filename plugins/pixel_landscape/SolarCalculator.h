// SolarCalculator.h — 无系统副作用的太阳位置与当地日期事件计算
#pragma once

#include <QDateTime>
#include <QVariantMap>

namespace landscape {
bool validCoordinates(double latitude, double longitude);
// 时刻使用 UTC 运算；事件属于 now 所在时区的当天，包括夏令时的短日与长日。
QVariantMap solarState(const QDateTime& now, double latitude, double longitude);
}
