// PluginManagerTest.cpp — 原生与 host 子插件单实例生命周期回归测试
#include "ConfigService.h"
#include "PluginManager.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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
    void initTestCase() {
        QCoreApplication::setApplicationVersion(QStringLiteral("0.2.1"));
    }

    void malformedManifestIsRejectedBeforePluginLoad() {
        const QString sourceRoot = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString pluginDir = QDir(root.path()).filePath(QStringLiteral("clock"));
        QVERIFY(QDir().mkpath(pluginDir));
        QVERIFY(QFile::copy(QDir(sourceRoot).filePath(QStringLiteral("clock/clock.dll")),
                            QDir(pluginDir).filePath(QStringLiteral("clock.dll"))));
        QFile manifest(QDir(pluginDir).filePath(QStringLiteral("metadata.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QCOMPARE(manifest.write("{"), qint64(1));
        manifest.close();

        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        manager.loadFrom(root.path(), {entry(QStringLiteral("clock"))});
        QVERIFY(!manager.loadedPlugin(QStringLiteral("clock")));
        manager.shutdownAll();
    }

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
        const QObject* clockViewModel = manager.loadedPlugin(clock.id)->viewModel;
        QVERIFY(menuViewModel);
        PluginConfigEntry updatedClock = clock;
        updatedClock.config.insert(QStringLiteral("format"), QStringLiteral("HH:mm"));
        QCOMPARE(manager.applyConfig(updatedClock),
                 PluginManager::ConfigApplyResult::Applied);
        QCOMPARE(manager.loadedPlugin(clock.id)->viewModel, clockViewModel);
        QCOMPARE(manager.applyConfig(menu),
                 PluginManager::ConfigApplyResult::Unsupported);
        const PluginConfigEntry missing = entry(QStringLiteral("missing_plugin"));
        QVERIFY(!manager.loadOne(pluginsDir, missing));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
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
        const QObject* childViewModel = manager.loadedPlugin(child.id)->viewModel;
        PluginConfigEntry updatedChild = child;
        updatedChild.config.insert(QStringLiteral("prefix"), QStringLiteral("hot"));
        QTRY_VERIFY_WITH_TIMEOUT(
            manager.applyConfig(updatedChild) == PluginManager::ConfigApplyResult::Applied,
            5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            childViewModel->property("values").toMap().value(QStringLiteral("text"))
                .toString().startsWith(QStringLiteral("hot")),
            5000);
        QCOMPARE(manager.loadedPlugin(child.id)->viewModel, childViewModel);
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
