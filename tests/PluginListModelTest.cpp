// PluginListModelTest.cpp — 插件列表健康状态角色回归测试
#include "PluginListModel.h"

#include <QTimeZone>
#include <QtTest>

class RestartableViewModel final : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool restart() {
        restarted = true;
        return true;
    }
    bool restarted = false;
};

class PluginListModelTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void exposesPluginHealthRoles() {
        PluginManager::LoadedPlugin plugin;
        plugin.id = QStringLiteral("health_test");
        plugin.position = QStringLiteral("left");
        plugin.capabilities = {QStringLiteral("barItem"), QStringLiteral("popup")};
        QObject pluginContext;
        plugin.pluginContext = &pluginContext;
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
        QCOMPARE(model.data(index, PluginListModel::CapabilitiesRole).toStringList(),
                 plugin.capabilities);
        QCOMPARE(model.data(index, PluginListModel::PluginContextRole).value<QObject*>(),
                 &pluginContext);

        const auto roles = model.roleNames();
        QCOMPARE(roles.value(PluginListModel::HealthStateRole), QByteArray("healthState"));
        QCOMPARE(roles.value(PluginListModel::LastErrorRole), QByteArray("lastError"));
        QCOMPARE(roles.value(PluginListModel::QueueDepthRole), QByteArray("queueDepth"));
        QCOMPARE(roles.value(PluginListModel::CapabilitiesRole), QByteArray("capabilities"));
        QCOMPARE(roles.value(PluginListModel::PluginContextRole), QByteArray("pluginContext"));
    }

    void mergesRuntimeHealthReports() {
        PluginManager::LoadedPlugin plugin;
        plugin.id = QStringLiteral("runtime_health");
        plugin.health.state = xuanhub::PluginState::Starting;
        plugin.health.startedAt = QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC);

        PluginListModel model;
        model.setPlugins({plugin});
        QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
        const QDateTime healthyAt = QDateTime::fromMSecsSinceEpoch(2000, QTimeZone::UTC);
        model.reportHealth(QStringLiteral("runtime_health"), {
            {QStringLiteral("state"), QStringLiteral("restarting")},
            {QStringLiteral("lastError"), QStringLiteral("IPC disconnected")},
            {QStringLiteral("lastHealthyAt"), healthyAt},
            {QStringLiteral("restartCount"), 3},
            {QStringLiteral("queueDepth"), 1},
        });

        QCOMPARE(changedSpy.count(), 1);
        const QModelIndex index = model.index(0);
        QCOMPARE(model.data(index, PluginListModel::HealthStateRole).toString(),
                 QStringLiteral("restarting"));
        QCOMPARE(model.data(index, PluginListModel::LastErrorRole).toString(),
                 QStringLiteral("IPC disconnected"));
        QCOMPARE(model.data(index, PluginListModel::LastHealthyAtRole).toDateTime(), healthyAt);
        QCOMPARE(model.data(index, PluginListModel::RestartCountRole).toInt(), 3);
        QCOMPARE(model.data(index, PluginListModel::QueueDepthRole).toInt(), 1);
        QCOMPARE(model.data(index, PluginListModel::StartedAtRole).toDateTime(),
                 plugin.health.startedAt);
    }

    void exposesPreloadFailureForAbsentPlugin() {
        PluginListModel model;
        QSignalSpy errorSpy(&model, &PluginListModel::loadErrorChanged);
        model.setLoadErrors({{QStringLiteral("broken"),
                              QStringLiteral("ABI mismatch")}});
        QCOMPARE(errorSpy.count(), 1);
        const QVariantMap health = model.health(QStringLiteral("broken"));
        QCOMPARE(health.value(QStringLiteral("state")).toString(),
                 QStringLiteral("failed"));
        QCOMPARE(health.value(QStringLiteral("lastError")).toString(),
                 QStringLiteral("ABI mismatch"));

        model.setLoadErrors({});
        QCOMPARE(errorSpy.count(), 2);
        QVERIFY(model.health(QStringLiteral("broken")).isEmpty());
    }

    void returnsHealthAndRestartsOnlyTargetPlugin() {
        RestartableViewModel firstViewModel;
        RestartableViewModel secondViewModel;
        PluginManager::LoadedPlugin first;
        first.id = QStringLiteral("first");
        first.viewModel = &firstViewModel;
        first.health.state = xuanhub::PluginState::CircuitOpen;
        first.health.lastError = QStringLiteral("worker exited");
        PluginManager::LoadedPlugin second;
        second.id = QStringLiteral("second");
        second.viewModel = &secondViewModel;

        PluginListModel model;
        model.setPlugins({first, second});
        const QVariantMap health = model.health(QStringLiteral("first"));
        QCOMPARE(health.value(QStringLiteral("state")).toString(),
                 QStringLiteral("circuitOpen"));
        QCOMPARE(health.value(QStringLiteral("lastError")).toString(),
                 QStringLiteral("worker exited"));

        QVERIFY(model.restartPlugin(QStringLiteral("first")));
        QVERIFY(firstViewModel.restarted);
        QVERIFY(!secondViewModel.restarted);
        QVERIFY(!model.restartPlugin(QStringLiteral("missing")));
    }

    void incrementallyUpdatesOnlyTargetRowsAndRegions() {
        QObject leftVm;
        QObject rightVm;
        PluginManager::LoadedPlugin left;
        left.id = QStringLiteral("left");
        left.position = QStringLiteral("left");
        left.order = 0;
        left.viewModel = &leftVm;
        PluginManager::LoadedPlugin right;
        right.id = QStringLiteral("right");
        right.position = QStringLiteral("right");
        right.order = 0;
        right.viewModel = &rightVm;

        PluginListModel model;
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
        model.setPlugins({left, right});
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(model.leftPlugins()->rowCount(), 1);
        QCOMPARE(model.rightPlugins()->rowCount(), 1);

        PluginManager::LoadedPlugin center;
        QObject centerVm;
        center.id = QStringLiteral("center");
        center.position = QStringLiteral("center");
        center.viewModel = &centerVm;
        QVERIFY(model.insertPlugin(center));
        QCOMPARE(insertedSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(model.centerPlugins()->rowCount(), 1);

        model.reportContentWidth(QStringLiteral("right"), 91);
        QVERIFY(model.updateLayout(QStringLiteral("right"), QStringLiteral("left"), -1, 140));
        QCOMPARE(model.leftPlugins()->rowCount(), 2);
        QCOMPARE(model.rightPlugins()->rowCount(), 0);
        QCOMPARE(model.data(model.index(0), PluginListModel::IdRole).toString(),
                 QStringLiteral("right"));
        QCOMPARE(model.data(model.index(0), PluginListModel::ViewModelRole).value<QObject*>(),
                 &rightVm);
        QCOMPARE(model.contentWidth(QStringLiteral("right")), 91);
        QCOMPARE(resetSpy.count(), 1);

        QVERIFY(model.removePlugin(QStringLiteral("center")));
        QCOMPARE(removedSpy.count(), 2); // 一次移动移除 + 一次显式移除
        QCOMPARE(model.centerPlugins()->rowCount(), 0);
        QVERIFY(!model.removePlugin(QStringLiteral("missing")));
        QCOMPARE(model.rowCount(), 2);
    }

    void exposesResponsiveWidthsAndOverflowWithoutChangingFixedWidth() {
        PluginManager::LoadedPlugin legacy;
        legacy.id = QStringLiteral("legacy");
        legacy.position = QStringLiteral("left");
        legacy.width = 120;
        PluginManager::LoadedPlugin adaptive;
        adaptive.id = QStringLiteral("adaptive");
        adaptive.position = QStringLiteral("right");
        adaptive.layout.declared = true;
        adaptive.layout.minWidth = 50;
        adaptive.layout.preferredWidth = 100;
        adaptive.layout.priority = 10;
        adaptive.layout.overflow = QStringLiteral("menu");

        PluginListModel model;
        model.setPlugins({legacy, adaptive});
        model.reportContentWidth(QStringLiteral("legacy"), 80);
        model.reportContentWidth(QStringLiteral("adaptive"), 100);
        model.setAvailableWidth(180);
        QCOMPARE(model.data(model.index(0), PluginListModel::EffectiveWidthRole).toInt(), 120);
        QCOMPARE(model.data(model.index(1), PluginListModel::EffectiveWidthRole).toInt(), 60);
        QVERIFY(model.data(model.index(1), PluginListModel::LayoutVisibleRole).toBool());

        model.setAvailableWidth(140);
        QCOMPARE(model.data(model.index(0), PluginListModel::EffectiveWidthRole).toInt(), 120);
        QVERIFY(!model.data(model.index(1), PluginListModel::LayoutVisibleRole).toBool());
        QVERIFY(model.data(model.index(1), PluginListModel::OverflowedRole).toBool());
        QCOMPARE(model.rightPlugins()->rowCount(), 0);
        QCOMPARE(model.overflowPluginModel()->rowCount(), 1);
        QCOMPARE(model.overflowPlugins().size(), 1);
        QCOMPARE(model.overflowPlugins().first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("adaptive"));

        // availableWidth=0 表示 taskbar/兼容模式，delegate 回退原有自然/固定宽绑定。
        model.setAvailableWidth(0);
        QCOMPARE(model.data(model.index(0), PluginListModel::EffectiveWidthRole).toInt(), 0);
        QVERIFY(model.data(model.index(1), PluginListModel::LayoutVisibleRole).toBool());
        QCOMPARE(model.rightPlugins()->rowCount(), 1);
        QCOMPARE(model.overflowPluginModel()->rowCount(), 0);
        QVERIFY(model.overflowPlugins().isEmpty());
    }
};

QTEST_GUILESS_MAIN(PluginListModelTest)
#include "PluginListModelTest.moc"
