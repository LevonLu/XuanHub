// PluginHealth.h — 跨宿主与插件运行时共享的健康状态数据
#pragma once

#include <QDateTime>
#include <QString>

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

struct PluginHealth {
    PluginState state = PluginState::Discovered;
    QString lastError;
    QDateTime lastErrorAt;
    QDateTime startedAt;
    QDateTime lastHealthyAt;
    int restartCount = 0;
    int consecutiveFailures = 0;
    qint64 loadDurationMs = 0;
    int queueDepth = 0;
};

} // namespace xuanhub
