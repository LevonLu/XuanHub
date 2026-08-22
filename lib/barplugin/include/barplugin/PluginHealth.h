// PluginHealth.h — 跨宿主与插件运行时共享的健康状态数据
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringView>

namespace xuanhub {

enum class PluginState {
    Discovered,
    Starting,
    Running,
    Degraded,
    Restarting,
    Failed,
    CircuitOpen,
    Disabled,
    Stopping,
    Stopped,
};

inline QString pluginStateKey(PluginState state) {
    switch (state) {
        case PluginState::Discovered:  return QStringLiteral("discovered");
        case PluginState::Starting:    return QStringLiteral("starting");
        case PluginState::Running:     return QStringLiteral("running");
        case PluginState::Degraded:    return QStringLiteral("degraded");
        case PluginState::Restarting:  return QStringLiteral("restarting");
        case PluginState::Failed:      return QStringLiteral("failed");
        case PluginState::CircuitOpen: return QStringLiteral("circuitOpen");
        case PluginState::Disabled:    return QStringLiteral("disabled");
        case PluginState::Stopping:    return QStringLiteral("stopping");
        case PluginState::Stopped:     return QStringLiteral("stopped");
    }
    return QStringLiteral("failed");
}

inline PluginState pluginStateFromKey(QStringView key, PluginState fallback) {
    if (key == u"discovered")  return PluginState::Discovered;
    if (key == u"starting")    return PluginState::Starting;
    if (key == u"running")     return PluginState::Running;
    if (key == u"degraded")    return PluginState::Degraded;
    if (key == u"restarting")  return PluginState::Restarting;
    if (key == u"failed")      return PluginState::Failed;
    if (key == u"circuitOpen") return PluginState::CircuitOpen;
    if (key == u"disabled")    return PluginState::Disabled;
    if (key == u"stopping")    return PluginState::Stopping;
    if (key == u"stopped")     return PluginState::Stopped;
    return fallback;
}

struct PluginHealth {
    PluginState state = PluginState::Discovered;
    QString lastError;
    QString lastErrorCode;
    QDateTime lastErrorAt;
    QDateTime startedAt;
    QDateTime lastHealthyAt;
    int restartCount = 0;
    int consecutiveFailures = 0;
    qint64 loadDurationMs = 0;
    int queueDepth = 0;
    qint64 processId = 0;
};

} // namespace xuanhub
