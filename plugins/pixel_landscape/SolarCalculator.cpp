// SolarCalculator.cpp — NOAA/Meeus 太阳几何与有界求根，不依赖联网服务
#include "SolarCalculator.h"
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace landscape {
namespace {
constexpr double pi = 3.14159265358979323846;
double rad(double degrees) { return degrees * pi / 180.0; }
double deg(double radians) { return radians * 180.0 / pi; }
double clamp(double v, double lo, double hi) { return std::clamp(v, lo, hi); }
double smooth(double lo, double hi, double x) {
    const double t = clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
struct Position { double elevation; double hourAngle; double declination; };
Position position(qint64 milliseconds, double latitude, double longitude) {
    // 采用完整儒略日，不使用限定 1901–2099 的电子表格近似。
    const double t = (milliseconds / 86400000.0 + 2440587.5 - 2451545.0) / 36525.0;
    const double l = std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
    const double m = rad(357.52911 + t * (35999.05029 - 0.0001537 * t));
    const double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double center = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
        + std::sin(2*m) * (0.019993 - 0.000101*t) + std::sin(3*m) * 0.000289;
    const double omega = rad(125.04 - 1934.136 * t);
    const double lambda = rad(l + center - 0.00569 - 0.00478 * std::sin(omega));
    const double obliquity = rad(23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60
                                + 0.00256 * std::cos(omega));
    const double declination = std::asin(std::sin(obliquity) * std::sin(lambda));
    const double y = std::pow(std::tan(obliquity / 2), 2);
    const double equation = 4 * deg(y * std::sin(2*rad(l)) - 2*e*std::sin(m)
        + 4*e*y*std::sin(m)*std::cos(2*rad(l)) - 0.5*y*y*std::sin(4*rad(l)) - 1.25*e*e*std::sin(2*m));
    double minutes = std::fmod(milliseconds / 60000.0 + equation + 4*longitude, 1440.0);
    if (minutes < 0) minutes += 1440;
    const double angle = minutes / 4 - 180;
    const double elevation = deg(std::asin(clamp(std::sin(rad(latitude))*std::sin(declination)
        + std::cos(rad(latitude))*std::cos(declination)*std::cos(rad(angle)), -1.0, 1.0)));
    return {elevation, angle, declination};
}
qint64 crossing(qint64 a, qint64 b, double threshold, bool rising, double lat, double lon) {
    while (b - a > 1000) {
        const qint64 mid = a + (b-a)/2;
        if ((position(mid, lat, lon).elevation < threshold) == rising) a = mid;
        else b = mid;
    }
    return a + (b-a)/2;
}
}

bool validCoordinates(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude)
        && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}

QVariantMap solarState(const QDateTime& now, double latitude, double longitude) {
    if (!now.isValid() || !validCoordinates(latitude, longitude)) return {{QStringLiteral("active"), false}};
    const auto p = position(now.toMSecsSinceEpoch(), latitude, longitude);
    const QString twilight = p.hourAngle < 0 ? QStringLiteral("dawn") : QStringLiteral("dusk");
    const bool low = p.elevation < 0;
    const double mix = low ? smooth(-12, 0, p.elevation) : smooth(0, 12, p.elevation);
    // 有限色板量化使日间静态地形不会因微小浮点变动反复重绘。
    const double blend = std::round(mix * 128) / 128;
    const double night = std::round((1 - smooth(-12, -2, p.elevation)) * 128) / 128;
    const double warm = std::round((1 - smooth(-4, 8, p.elevation)) * 128) / 128;
    // 正午和太阳午夜均连续；高纬度夏季午夜仍有晨昏光，不能在时角回绕处跳色。
    const double twilightMix = std::round(smooth(-0.5,0.5,std::sin(rad(p.hourAngle)))*128)/128;
    QVariantMap result{{QStringLiteral("active"), true}, {QStringLiteral("elevation"), p.elevation},
        {QStringLiteral("from"), low ? QStringLiteral("night") : twilight},
        {QStringLiteral("to"), low ? twilight : QStringLiteral("day")}, {QStringLiteral("mix"), blend},
        {QStringLiteral("night"), night}, {QStringLiteral("warm"), warm},
        {QStringLiteral("twilightMix"), twilightMix},
        {QStringLiteral("mist"), std::round((1-twilightMix)*(1-std::abs(blend*2-1))*64)/128}};
    const double denominator = std::cos(rad(latitude)) * std::cos(p.declination);
    const double boundary = (std::sin(rad(-0.833)) - std::sin(rad(latitude))*std::sin(p.declination));
    const double cosHour = std::abs(denominator) > 1e-12 ? boundary / denominator
        : (boundary < 0 ? -2.0 : 2.0);
    const QString kind = cosHour < -1 ? QStringLiteral("polarDay")
        : cosHour > 1 ? QStringLiteral("polarNight") : QStringLiteral("normal");
    const double halfDay = deg(std::acos(clamp(cosHour, -1, 1)));
    const double x = kind == QStringLiteral("polarDay") ? 0.5 + 0.42 * std::sin(rad(p.hourAngle))
        : 0.08 + 0.84 * clamp((p.hourAngle + halfDay) / std::max(0.001, 2*halfDay), 0.0, 1.0);
    result.insert(QStringLiteral("sunX"), x);
    result.insert(QStringLiteral("sunHeight"), std::sin(rad(std::max(0.0, p.elevation))));
    result.insert(QStringLiteral("sunOpacity"), kind == QStringLiteral("polarNight") ? 0.0 : smooth(-0.833, 0.5, p.elevation));
    result.insert(QStringLiteral("dayKind"), kind);
    result.insert(QStringLiteral("phase"), p.elevation <= -12 ? QStringLiteral("night")
        : p.elevation >= 12 ? QStringLiteral("day") : twilight);

    const auto zone = now.timeZone();
    const qint64 begin = now.date().startOfDay(zone).toMSecsSinceEpoch();
    const qint64 end = now.date().addDays(1).startOfDay(zone).toMSecsSinceEpoch();
    double previous = position(begin, latitude, longitude).elevation;
    double peak = previous;
    qint64 peakTime = begin;
    for (qint64 a = begin; a < end;) {
        const qint64 b = std::min(a + 300000, end);
        const double current = position(b, latitude, longitude).elevation;
        for (int threshold = 0; threshold < 2; ++threshold) {
            const double h = threshold == 0 ? -0.833 : -6.0;
            const bool rise = previous < h && current >= h;
            const bool set = previous >= h && current < h;
            if (rise || set) {
                const QString key = threshold == 0 ? (rise ? QStringLiteral("sunrise") : QStringLiteral("sunset"))
                    : (rise ? QStringLiteral("dawn") : QStringLiteral("dusk"));
                result.insert(key, QDateTime::fromMSecsSinceEpoch(crossing(a,b,h,rise,latitude,longitude), zone));
            }
        }
        if (current > peak) { peak = current; peakTime = b; }
        previous = current;
        a = b;
    }
    // 三分搜索只细化当天最高点；极区也不除以不存在的昼长。
    qint64 left = std::max(begin, peakTime-300000), right = std::min(end, peakTime+300000);
    while (right-left > 1000) {
        const qint64 a = left+(right-left)/3, b = right-(right-left)/3;
        if (position(a,latitude,longitude).elevation < position(b,latitude,longitude).elevation) left=a;
        else right=b;
    }
    result.insert(QStringLiteral("solarNoon"), QDateTime::fromMSecsSinceEpoch(left+(right-left)/2, zone));
    result.insert(QStringLiteral("timeZone"), now.timeZoneAbbreviation());
    return result;
}
}
