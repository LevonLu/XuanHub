// PluginManagerTest.cpp — 原生与 host 子插件单实例生命周期回归测试
#include "ConfigService.h"
#include "PluginManager.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QtTest>

namespace {
PluginConfigEntry entry(const QString& id, const QVariantMap& config = {}) {
    PluginConfigEntry value;
    value.id = id;
    value.position = QStringLiteral("right");
    value.config = config;
    return value;
}
} // namespace

class PluginManagerTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void nativePluginCanStopAndReloadWithoutTouchingPeer() {
        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        QVERIFY2(!pluginsDir.isEmpty(), "XUANHUB_TEST_PLUGINS is required");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        const PluginConfigEntry clock = entry(QStringLiteral("clock"));
        const PluginConfigEntry menu = entry(QStringLiteral("xuanhub_menu"));
        manager.loadFrom(pluginsDir, {clock, menu});

        QVERIFY(manager.loadedPlugin(clock.id));
        const QObject* menuViewModel = manager.loadedPlugin(menu.id)->viewModel;
        QVERIFY(menuViewModel);
        QVERIFY(manager.stopOne(clock.id));
        QVERIFY(!manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        bus.publish(QStringLiteral("app.quickmenu.state"));

        QVERIFY(manager.loadOne(pluginsDir, clock));
        QVERIFY(manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        QVERIFY(manager.restartOne(pluginsDir, clock));
        QVERIFY(manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        manager.shutdownAll();
    }

    void pythonSubPluginCanRestartWithoutStoppingNativePlugin() {
        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        const PluginConfigEntry host = entry(QStringLiteral("pybridge"));
        const PluginConfigEntry child = entry(QStringLiteral("py_hello"),
                                               {{QStringLiteral("intervalMs"), 1000}});
        const PluginConfigEntry clock = entry(QStringLiteral("clock"));
        manager.loadFrom(pluginsDir, {host, child, clock});

        QVERIFY2(manager.loadedPlugin(child.id), "py_hello failed to start in test runtime");
        const QObject* clockViewModel = manager.loadedPlugin(clock.id)->viewModel;
        QVERIFY(manager.stopOne(child.id));
        QVERIFY(!manager.loadedPlugin(child.id));
        QCOMPARE(manager.loadedPlugin(clock.id)->viewModel, clockViewModel);
        QVERIFY(manager.loadOne(pluginsDir, child));
        QVERIFY(manager.loadedPlugin(child.id));
        QCOMPARE(manager.loadedPlugin(clock.id)->viewModel, clockViewModel);
        QCOMPARE(manager.dependentPluginIds(host.id), QStringList{child.id});
        manager.shutdownAll();
    }
};

QTEST_GUILESS_MAIN(PluginManagerTest)
#include "PluginManagerTest.moc"
