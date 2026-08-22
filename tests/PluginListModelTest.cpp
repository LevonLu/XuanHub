// PluginListModelTest.cpp — 插件列表健康状态角色回归测试
#include "PluginListModel.h"

#include <QTimeZone>
#include <QtTest>

class PluginListModelTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void exposesPluginHealthRoles() {
        PluginManager::LoadedPlugin plugin;
        plugin.id = QStringLiteral("health_test");
        plugin.position = QStringLiteral("left");
        plugin.health.state = xuanhub::PluginState::CircuitOpen;
        plugin.health.lastError = QStringLiteral("worker crashed");
        plugin.health.lastErrorAt = QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC);
        plugin.health.startedAt = QDateTime::fromMSecsSinceEpoch(2000, QTimeZone::UTC);
        plugin.health.lastHealthyAt = QDateTime::fromMSecsSinceEpoch(3000, QTimeZone::UTC);
        plugin.health.restartCount = 4;
        plugin.health.consecutiveFailures = 5;
        plugin.health.loadDurationMs = 27;
        plugin.health.queueDepth = 2;

        PluginListModel model;
        model.setPlugins({plugin});
        const QModelIndex index = model.index(0);
        QCOMPARE(model.data(index, PluginListModel::HealthStateRole).toString(),
                 QStringLiteral("circuitOpen"));
        QCOMPARE(model.data(index, PluginListModel::LastErrorRole).toString(),
                 QStringLiteral("worker crashed"));
        QCOMPARE(model.data(index, PluginListModel::LastErrorAtRole).toDateTime(),
                 plugin.health.lastErrorAt);
        QCOMPARE(model.data(index, PluginListModel::StartedAtRole).toDateTime(),
                 plugin.health.startedAt);
        QCOMPARE(model.data(index, PluginListModel::LastHealthyAtRole).toDateTime(),
                 plugin.health.lastHealthyAt);
        QCOMPARE(model.data(index, PluginListModel::RestartCountRole).toInt(), 4);
        QCOMPARE(model.data(index, PluginListModel::ConsecutiveFailuresRole).toInt(), 5);
        QCOMPARE(model.data(index, PluginListModel::LoadDurationMsRole).toLongLong(), 27);
        QCOMPARE(model.data(index, PluginListModel::QueueDepthRole).toInt(), 2);

        const auto roles = model.roleNames();
        QCOMPARE(roles.value(PluginListModel::HealthStateRole), QByteArray("healthState"));
        QCOMPARE(roles.value(PluginListModel::LastErrorRole), QByteArray("lastError"));
        QCOMPARE(roles.value(PluginListModel::QueueDepthRole), QByteArray("queueDepth"));
    }
};

QTEST_GUILESS_MAIN(PluginListModelTest)
#include "PluginListModelTest.moc"
