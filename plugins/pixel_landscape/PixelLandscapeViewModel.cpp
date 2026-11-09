// PixelLandscapeViewModel.cpp — 校时只改变场景状态，不驱动逐帧绘制
#include "PixelLandscapeViewModel.h"
#include <QStringList>

PixelLandscapeViewModel::PixelLandscapeViewModel(QObject* parent) : QObject(parent) {
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, [this] { refreshAt(QDateTime::currentDateTime()); });
}

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
    for (const QString& key : {QStringLiteral("animationsEnabled"), QStringLiteral("rareEventsEnabled")})
        if (config.contains(key) && config.value(key).metaType().id() != QMetaType::Bool)
            return QStringLiteral("%1 must be boolean").arg(key);
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
    m_timeMode = config.value(QStringLiteral("timeMode"), QStringLiteral("auto")).toString();
    m_weather = config.value(QStringLiteral("weather"), QStringLiteral("clear")).toString();
    m_scene = config.value(QStringLiteral("scene"), QStringLiteral("cabin")).toString();
    m_frameStyle = config.value(QStringLiteral("frameStyle"), QStringLiteral("wood")).toString();
    m_animationsEnabled = config.value(QStringLiteral("animationsEnabled"), true).toBool();
    m_rareEventsEnabled = config.value(QStringLiteral("rareEventsEnabled"), true).toBool();
    resync();
    Q_EMIT configChanged();
    return true;
}

void PixelLandscapeViewModel::refreshAt(const QDateTime& now, bool force) {
    const QString next = m_timeMode == QStringLiteral("auto") ? phaseAt(now.time()) : m_timeMode;
    const qint64 delta = m_lastCheck.isValid() ? m_lastCheck.msecsTo(now) : -1;
    m_lastCheck = now;
    // 唤醒、时钟回拨和主动重同步直接落到当前画面，避免补播历史过渡。
    if (next == m_phase && !force && delta >= 0 && delta <= 90000) return;
    m_previousPhase = m_phase;
    m_phase = next;
    m_transitionAllowed = !force && delta >= 0 && delta <= 90000 && m_previousPhase != next;
    Q_EMIT phaseChanged();
}

void PixelLandscapeViewModel::resync() { refreshAt(QDateTime::currentDateTime(), true); }
void PixelLandscapeViewModel::start() { resync(); m_timer.start(); }
void PixelLandscapeViewModel::stop() { m_timer.stop(); }
void PixelLandscapeViewModel::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    Q_EMIT pausedChanged();
}
