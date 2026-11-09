// PixelLandscapeViewModel.cpp — 校时只改变场景状态，不驱动逐帧绘制
#include "PixelLandscapeViewModel.h"
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QCoreApplication>
#include <windows.h>
#include <cmath>
#include "SolarCalculator.h"
#include "barplugin/HostServices.h"

PixelLandscapeViewModel::PixelLandscapeViewModel(QObject* parent, LandscapeLocation* location)
    : QObject(parent), m_location(location ? location : new WindowsLandscapeLocation(this)) {
    if (location) location->setParent(this);
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const auto now = QDateTime::currentDateTime();
        checkLocation(now);
        refreshAt(now);
    });
    connect(m_location, &LandscapeLocation::finished, this, &PixelLandscapeViewModel::locationFinished);
}

PixelLandscapeViewModel::~PixelLandscapeViewModel() { stop(); }

QString PixelLandscapeViewModel::configurationError(const QVariantMap& config) {
    const auto enumError = [&config](const QString& key, const QStringList& values) -> QString {
        if (!config.contains(key)) return {};
        const QVariant value = config.value(key);
        if (value.metaType().id() != QMetaType::QString || !values.contains(value.toString()))
            return QStringLiteral("invalid %1").arg(key);
        return {};
    };
    QString error = enumError(QStringLiteral("timeMode"),
        {QStringLiteral("auto"), QStringLiteral("dawn"), QStringLiteral("day"), QStringLiteral("dusk"), QStringLiteral("night")});
    if (!error.isEmpty()) return error;
    error = enumError(QStringLiteral("locationSource"), {QStringLiteral("system"), QStringLiteral("manual")});
    if (!error.isEmpty()) return error;
    error = enumError(QStringLiteral("scene"),
        {QStringLiteral("cabin"), QStringLiteral("lighthouse"), QStringLiteral("street"),
         QStringLiteral("campsite"), QStringLiteral("railway"), QStringLiteral("island")});
    if (!error.isEmpty()) return error;
    error = enumError(QStringLiteral("weather"),
        {QStringLiteral("clear"), QStringLiteral("rain"), QStringLiteral("snow")});
    if (!error.isEmpty()) return error;
    error = enumError(QStringLiteral("frameStyle"),
        {QStringLiteral("none"), QStringLiteral("thin"), QStringLiteral("wood"), QStringLiteral("mat"), QStringLiteral("metal")});
    if (!error.isEmpty()) return error;
    for (const QString& key : {QStringLiteral("animationsEnabled"), QStringLiteral("rareEventsEnabled"), QStringLiteral("manualLocationEnabled")})
        if (config.contains(key) && config.value(key).metaType().id() != QMetaType::Bool)
            return QStringLiteral("%1 must be boolean").arg(key);
    const bool hasLat = config.contains(QStringLiteral("latitude"));
    const bool hasLon = config.contains(QStringLiteral("longitude"));
    if (hasLat != hasLon) return QStringLiteral("latitude and longitude must be paired");
    for (const QString& key : {QStringLiteral("latitude"), QStringLiteral("longitude")}) {
        if (!config.contains(key)) continue;
        const auto value = config.value(key);
        const auto type = value.metaType().id();
        if (type != QMetaType::Double && type != QMetaType::Int && type != QMetaType::LongLong)
            return QStringLiteral("%1 must be numeric").arg(key);
    }
    if (hasLat && !landscape::validCoordinates(config.value(QStringLiteral("latitude")).toDouble(),
                                               config.value(QStringLiteral("longitude")).toDouble()))
        return QStringLiteral("coordinates out of range");
    const bool manual = config.value(QStringLiteral("manualLocationEnabled"), false).toBool();
    if (manual && !hasLat) return QStringLiteral("manual location requires coordinates");
    if (config.value(QStringLiteral("locationSource")).toString() == QStringLiteral("manual") && !manual)
        return QStringLiteral("manual location must be enabled");
    return {};
}

QString PixelLandscapeViewModel::phaseAt(const QTime& time) {
    const int hour = time.hour();
    if (hour >= 5 && hour < 8) return QStringLiteral("dawn");
    if (hour >= 8 && hour < 17) return QStringLiteral("day");
    if (hour >= 17 && hour < 20) return QStringLiteral("dusk");
    return QStringLiteral("night");
}

bool PixelLandscapeViewModel::applyConfig(const QVariantMap& config) {
    if (!configurationError(config).isEmpty()) return false;
    // 只有定位相关配置变化才取消请求；切场景和画框不应打断正在取得的位置。
    const bool locationChanged = m_timeMode != config.value(QStringLiteral("timeMode"), QStringLiteral("auto")).toString()
        || m_locationSource != config.value(QStringLiteral("locationSource"), QStringLiteral("system")).toString()
        || m_manualLocationEnabled != config.value(QStringLiteral("manualLocationEnabled"), false).toBool()
        || m_latitude != config.value(QStringLiteral("latitude"), 0).toDouble()
        || m_longitude != config.value(QStringLiteral("longitude"), 0).toDouble();
    if (locationChanged) { cancelLocation(); m_lastAttempt = {}; }
    m_timeMode = config.value(QStringLiteral("timeMode"), QStringLiteral("auto")).toString();
    m_weather = config.value(QStringLiteral("weather"), QStringLiteral("clear")).toString();
    m_scene = config.value(QStringLiteral("scene"), QStringLiteral("cabin")).toString();
    m_frameStyle = config.value(QStringLiteral("frameStyle"), QStringLiteral("wood")).toString();
    m_animationsEnabled = config.value(QStringLiteral("animationsEnabled"), true).toBool();
    m_rareEventsEnabled = config.value(QStringLiteral("rareEventsEnabled"), true).toBool();
    m_locationSource = config.value(QStringLiteral("locationSource"), QStringLiteral("system")).toString();
    if (m_locationSource == QStringLiteral("manual") || m_timeMode != QStringLiteral("auto")) m_locationError.clear();
    m_manualLocationEnabled = config.value(QStringLiteral("manualLocationEnabled"), false).toBool();
    m_latitude = config.value(QStringLiteral("latitude"), 0).toDouble();
    m_longitude = config.value(QStringLiteral("longitude"), 0).toDouble();
    if (m_started) checkLocation(QDateTime::currentDateTime());
    resync();
    Q_EMIT configChanged();
    return true;
}

void PixelLandscapeViewModel::refreshAt(const QDateTime& now, bool force) {
    const bool automatic = m_timeMode == QStringLiteral("auto");
    const qint64 age = m_positionAt.isValid() ? m_positionAt.msecsTo(now) : -1;
    const bool cached = age >= 0 && age <= 24LL*60*60*1000;
    QString source = automatic ? QStringLiteral("clock") : QStringLiteral("preset");
    QVariantMap solar{{QStringLiteral("active"), false}};
    if (automatic && m_locationSource == QStringLiteral("system") && cached) {
        solar = landscape::solarState(now, m_systemLatitude, m_systemLongitude);
        source = m_livePosition ? QStringLiteral("system") : QStringLiteral("cache");
    } else if (automatic && m_manualLocationEnabled) {
        solar = landscape::solarState(now, m_latitude, m_longitude);
        source = QStringLiteral("manual");
    }
    if (solar != m_solarState) { m_solarState = solar; Q_EMIT solarStateChanged(); }
    const QVariantMap state{{QStringLiteral("source"), source}, {QStringLiteral("error"), m_locationError},
        {QStringLiteral("pending"), m_pending}, {QStringLiteral("automatic"), automatic},
        {QStringLiteral("systemSelected"), m_locationSource == QStringLiteral("system")}};
    if (state != m_locationState) { m_locationState = state; Q_EMIT locationStateChanged(); }
    const QString next = solar.value(QStringLiteral("active")).toBool() ? solar.value(QStringLiteral("phase")).toString()
        : automatic ? phaseAt(now.time()) : m_timeMode;
    const qint64 delta = m_lastCheck.isValid() ? m_lastCheck.msecsTo(now) : -1;
    m_lastCheck = now;
    // 唤醒、时钟回拨和主动重同步直接落到当前画面，避免补播历史过渡。
    if (next == m_phase && !force && delta >= 0 && delta <= 90000) return;
    m_previousPhase = m_phase;
    m_phase = next;
    m_transitionAllowed = !solar.value(QStringLiteral("active")).toBool() && !force
        && delta >= 0 && delta <= 90000 && m_previousPhase != next;
    Q_EMIT phaseChanged();
}

void PixelLandscapeViewModel::resync() {
    const auto now = QDateTime::currentDateTime();
    if (m_started) checkLocation(now);
    refreshAt(now, true);
}
void PixelLandscapeViewModel::start() {
    if (m_started) return;
    m_started = true;
    QCoreApplication::instance()->installNativeEventFilter(this);
    resync(); m_timer.start();
}
void PixelLandscapeViewModel::stop() {
    m_started = false; m_timer.stop(); cancelLocation();
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
}
bool PixelLandscapeViewModel::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) {
    if (!m_started || !message || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")) return false;
    const auto* msg = static_cast<const MSG*>(message);
    const bool resume = msg->message == WM_POWERBROADCAST
        && (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND);
    if ((resume || msg->message == WM_TIMECHANGE) && !m_resyncQueued) {
        // 多个宿主窗口会重复收到广播；离开原生消息栈后合并校时，暂停动画时也生效。
        m_resyncQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_resyncQueued = false;
            if (m_started) resync();
        });
    }
    return false;
}
void PixelLandscapeViewModel::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    Q_EMIT pausedChanged();
}

void PixelLandscapeViewModel::setStorage(xuanhub::IPluginStorage* storage) {
    m_storage = storage;
    if (!storage) return;
    const auto read = storage->readBytes(QStringLiteral("location.json"));
    if (!read.success) return;
    const auto value = QJsonDocument::fromJson(read.value.toByteArray()).object();
    if (!value.value(QStringLiteral("latitude")).isDouble() || !value.value(QStringLiteral("longitude")).isDouble()) return;
    const double lat = value.value(QStringLiteral("latitude")).toDouble();
    const double lon = value.value(QStringLiteral("longitude")).toDouble();
    if (!landscape::validCoordinates(lat,lon)) return;
    m_systemLatitude = lat; m_systemLongitude = lon;
    m_positionAt = QDateTime::fromString(value.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
}
void PixelLandscapeViewModel::cancelLocation() {
    ++m_requestId;
    m_pending = false;
    m_location->cancel();
}
void PixelLandscapeViewModel::revokeLocation() {
    cancelLocation();
    m_positionAt = {};
    m_livePosition = false;
    if (m_storage) m_storage->removePath(QStringLiteral("location.json"));
}
void PixelLandscapeViewModel::checkLocation(const QDateTime& now, bool interactive) {
    if (!m_started || m_timeMode != QStringLiteral("auto") || m_locationSource != QStringLiteral("system")) return;
    const QString permission = m_location->access();
    if (permission == QStringLiteral("denied")) {
        revokeLocation();
        m_locationError = QStringLiteral("denied");
    }
    if (m_pending) return;
    if (!interactive && permission != QStringLiteral("allowed")) {
        m_locationError = permission;
        return;
    }
    const qint64 elapsed = m_lastAttempt.isValid() ? m_lastAttempt.msecsTo(now) : -1;
    if (!interactive && elapsed >= 0 && elapsed < 30*60*1000) return;
    m_lastAttempt = now;
    m_pending = true;
    m_locationError.clear();
    m_location->request(++m_requestId, interactive);
}
void PixelLandscapeViewModel::requestLocation() {
    checkLocation(QDateTime::currentDateTime(), true);
    refreshAt(QDateTime::currentDateTime(), true);
}
void PixelLandscapeViewModel::locationFinished(quint64 id, const QString& error, double latitude,
                                               double longitude, const QDateTime& timestamp) {
    if (!m_started || !m_pending || id != m_requestId) return;
    m_pending = false;
    m_locationError = error;
    if (error == QStringLiteral("denied")) revokeLocation();
    else if (error.isEmpty()) {
        const qint64 age = timestamp.msecsTo(QDateTime::currentDateTimeUtc());
        if (!timestamp.isValid() || age < 0 || age > 24LL*60*60*1000 || !landscape::validCoordinates(latitude,longitude))
            m_locationError = QStringLiteral("invalid_position");
        else {
            m_systemLatitude = latitude; m_systemLongitude = longitude;
            m_positionAt = timestamp; m_livePosition = true;
            if (m_storage) {
                const QJsonObject record{{QStringLiteral("latitude"),latitude},{QStringLiteral("longitude"),longitude},
                    {QStringLiteral("timestamp"), timestamp.toUTC().toString(Qt::ISODateWithMs)}};
                if (!m_storage->writeBytes(QStringLiteral("location.json"), QJsonDocument(record).toJson(QJsonDocument::Compact)).success)
                    m_locationError = QStringLiteral("cache_write_failed");
            }
        }
    } else m_livePosition = false;
    refreshAt(QDateTime::currentDateTime(), true);
}
