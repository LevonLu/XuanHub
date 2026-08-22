// PluginManagerTest.cpp — 原生与 host 子插件单实例生命周期回归测试
#include "ConfigService.h"
#include "PluginActionRegistry.h"
#include "PluginManager.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
        QVERIFY(!manager.loadError(QStringLiteral("clock")).isEmpty());
        manager.shutdownAll();
    }

    void escapingPythonManifestDoesNotExecuteWorker() {
        const QString pythonPlugins = QDir(QCoreApplication::applicationDirPath())
                                          .filePath(QStringLiteral("plugins_py"));
        QTemporaryDir pluginDirectory(
            QDir(pythonPlugins).filePath(QStringLiteral("escape-test-XXXXXX")));
        QVERIFY(pluginDirectory.isValid());
        const QString leaf = QFileInfo(pluginDirectory.path()).fileName();
        const QString outsideQml = QDir(pythonPlugins).filePath(leaf + QStringLiteral(".qml"));
        QFile qml(outsideQml);
        QVERIFY(qml.open(QIODevice::WriteOnly));
        QVERIFY(qml.write("import QtQuick\nItem {}") > 0);
        qml.close();

        QFile python(QDir(pluginDirectory.path()).filePath(QStringLiteral("main.py")));
        QVERIFY(python.open(QIODevice::WriteOnly));
        QVERIFY(python.write(
            "from pathlib import Path\n"
            "Path(__file__).with_name('executed').write_text('bad')\n"
            "def update(): return {}\n") > 0);
        python.close();
        const QString pluginId = QStringLiteral("py_escape_manifest_test");
        const QJsonObject object{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Escape Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("qml"), QStringLiteral("../")
                                       + QFileInfo(outsideQml).fileName()}};
        QFile manifest(QDir(pluginDirectory.path()).filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QVERIFY(manifest.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0);
        manifest.close();

        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        manager.loadFrom(pluginsDir,
                         {entry(QStringLiteral("pybridge")), entry(pluginId)});
        QVERIFY(!manager.loadedPlugin(pluginId));
        QVERIFY(!manager.loadError(pluginId).isEmpty());
        QVERIFY(!QFileInfo(QDir(pluginDirectory.path()).filePath(
                              QStringLiteral("executed"))).exists());
        manager.shutdownAll();
        QVERIFY(QFile::remove(outsideQml));
    }

    void nativePluginCanStopAndReloadWithoutTouchingPeer() {
        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        QVERIFY2(!pluginsDir.isEmpty(), "XUANHUB_TEST_PLUGINS is required");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        const PluginConfigEntry clock = entry(QStringLiteral("clock"));
        const PluginConfigEntry sysinfo = entry(QStringLiteral("sysinfo"));
        const PluginConfigEntry menu = entry(QStringLiteral("xuanhub_menu"));
        manager.loadFrom(pluginsDir, {clock, sysinfo, menu});

        QVERIFY(manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(clock.id)->apiVersion, 2);
        QSignalSpy pluginsChanged(&manager, &PluginManager::pluginsChanged);
        const QVariantMap undeclaredAction = manager.invokeAction(
            clock.id, QStringLiteral("not_declared"));
        QVERIFY(!undeclaredAction.value(QStringLiteral("accepted")).toBool());
        QCOMPARE(undeclaredAction.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("action_not_declared"));
        const auto* migration = manager.migrationSuggestion(clock.id);
        QVERIFY(migration);
        QVERIFY(migration->ready());
        QCOMPARE(migration->currentVersion, 1);
        QCOMPARE(migration->targetVersion, 2);
        QCOMPARE(migration->config.value(QStringLiteral("format")).toString(),
                 QStringLiteral("HH:mm:ss"));
        QCOMPARE(manager.loadedPlugin(menu.id)->apiVersion, 1);
        QCOMPARE(manager.loadedPlugin(sysinfo.id)->actions.size(), 1);
        const QVariantMap refreshed = manager.invokeAction(
            sysinfo.id, QStringLiteral("refresh"));
        QVERIFY(refreshed.value(QStringLiteral("accepted")).toBool());
        QVERIFY(!refreshed.value(QStringLiteral("pending")).toBool());
        const QObject* menuViewModel = manager.loadedPlugin(menu.id)->viewModel;
        const QObject* clockViewModel = manager.loadedPlugin(clock.id)->viewModel;
        QVERIFY(menuViewModel);
        PluginConfigEntry updatedClock = clock;
        updatedClock.config.insert(QStringLiteral("format"), QStringLiteral("HH:mm"));
        const auto validConfig = manager.validateConfig(clock.id, updatedClock.config);
        QVERIFY(validConfig.supported);
        QVERIFY(validConfig.valid);
        const auto invalidConfig = manager.validateConfig(
            clock.id, {{QStringLiteral("format"), QVariantMap{{QStringLiteral("bad"), true}}}});
        QVERIFY(invalidConfig.supported);
        QVERIFY(!invalidConfig.valid);
        QVERIFY(!invalidConfig.error.isEmpty());
        QCOMPARE(manager.applyConfig(updatedClock),
                 PluginManager::ConfigApplyResult::Applied);
        QCOMPARE(manager.loadedPlugin(clock.id)->viewModel, clockViewModel);
        QCOMPARE(manager.applyConfig(menu),
                 PluginManager::ConfigApplyResult::Unsupported);
        QVERIFY(!manager.validateConfig(menu.id, {}).supported);
        const PluginConfigEntry missing = entry(QStringLiteral("missing_plugin"));
        QVERIFY(!manager.loadOne(pluginsDir, missing));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        QVERIFY(manager.stopOne(clock.id));
        QCOMPARE(pluginsChanged.count(), 1);
        QVERIFY(!manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        bus.publish(QStringLiteral("app.quickmenu.state"));

        QVERIFY(manager.loadOne(pluginsDir, clock));
        QCOMPARE(pluginsChanged.count(), 2);
        QVERIFY(manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        QVERIFY(manager.restartOne(pluginsDir, clock));
        QVERIFY(manager.loadedPlugin(clock.id));
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        manager.shutdownAll();
    }

    void futureConfigVersionDisablesOnlyTargetPlugin() {
        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        PluginConfigEntry futureClock = entry(QStringLiteral("clock"));
        futureClock.configVersion = 3;
        const PluginConfigEntry menu = entry(QStringLiteral("xuanhub_menu"));

        manager.loadFrom(pluginsDir, {futureClock, menu});
        QVERIFY(!manager.loadedPlugin(futureClock.id));
        QVERIFY(manager.loadError(futureClock.id).contains(QStringLiteral("migration")));
        const auto* migration = manager.migrationSuggestion(futureClock.id);
        QVERIFY(migration);
        QVERIFY(!migration->ready());
        QVERIFY(!migration->error.isEmpty());
        QVERIFY(manager.loadedPlugin(menu.id));
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

    void pythonSubPluginReportsMigrationSuggestion() {
        const QString pythonPlugins = QDir(QCoreApplication::applicationDirPath())
                                          .filePath(QStringLiteral("plugins_py"));
        QTemporaryDir pluginDirectory(
            QDir(pythonPlugins).filePath(QStringLiteral("migration-test-XXXXXX")));
        QVERIFY(pluginDirectory.isValid());
        const QString pluginId = QStringLiteral("py_migration_test");

        QFile qml(QDir(pluginDirectory.path()).filePath(QStringLiteral("Migration.qml")));
        QVERIFY(qml.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(qml.write("import QtQuick\nItem { property var plugin }") > 0);
        qml.close();
        QFile python(QDir(pluginDirectory.path()).filePath(QStringLiteral("main.py")));
        QVERIFY(python.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(python.write(
            "CONFIG_VERSION = 2\n"
            "state = {}\n"
            "def migrate_config(old_version, config):\n"
            "    config['prefix'] = 'migrated'\n"
            "    config['fromVersion'] = old_version\n"
            "    return config\n"
            "def validate_config(config):\n"
            "    return {'ok': config.get('prefix') == 'migrated', 'errors': []}\n"
            "def init(config): state.update(config)\n"
            "def update():\n"
            "    values = dict(state)\n"
            "    values['actionStates'] = {\n"
            "        'refresh': {'enabled': True},\n"
            "        'disabled': {'enabled': False}}\n"
            "    return values\n"
            "def invoke(name, args): state['invoked'] = name\n") > 0);
        python.close();
        QFile manifest(QDir(pluginDirectory.path()).filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(manifest.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Migration Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("configVersion"), 2},
            {QStringLiteral("qml"), QStringLiteral("Migration.qml")},
            {QStringLiteral("actions"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("refresh")},
                    {QStringLiteral("label"), QStringLiteral("立即刷新")},
                    {QStringLiteral("placements"),
                     QJsonArray{QStringLiteral("quickMenu"), QStringLiteral("tray")}}},
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("disabled")},
                    {QStringLiteral("label"), QStringLiteral("不可用动作")},
                    {QStringLiteral("placements"),
                     QJsonArray{QStringLiteral("quickMenu")}}}}},
        }).toJson(QJsonDocument::Compact)) > 0);
        manifest.close();

        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, QDir::tempPath());
        PluginConfigEntry child = entry(pluginId);
        child.configVersion = 1;
        manager.loadFrom(pluginsDir,
                         {entry(QStringLiteral("pybridge")), child});
        QVERIFY(manager.loadedPlugin(pluginId));
        QCOMPARE(manager.loadedPlugin(pluginId)->actions.size(), 2);
        QCOMPARE(manager.loadedPlugin(pluginId)->actions.first().id,
                 QStringLiteral("refresh"));
        QTRY_VERIFY_WITH_TIMEOUT(manager.migrationSuggestion(pluginId), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            manager.loadedPlugin(pluginId)->viewModel->property("values").toMap()
                .contains(QStringLiteral("actionStates")), 5000);
        PluginActionRegistry actions(&manager, nullptr);
        const QVariantList quickActions = actions.actions(QStringLiteral("quickMenu"));
        QCOMPARE(quickActions.size(), 2);
        QVERIFY(quickActions.at(0).toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(!quickActions.at(1).toMap().value(QStringLiteral("enabled")).toBool());
        const auto* suggestion = manager.migrationSuggestion(pluginId);
        QVERIFY(suggestion->ready());
        QCOMPARE(suggestion->currentVersion, 1);
        QCOMPARE(suggestion->targetVersion, 2);
        QCOMPARE(suggestion->config.value(QStringLiteral("prefix")).toString(),
                 QStringLiteral("migrated"));
        QCOMPARE(suggestion->config.value(QStringLiteral("fromVersion")).toInt(), 1);
        const auto accepted = manager.validateConfig(pluginId, suggestion->config);
        QVERIFY(accepted.supported);
        QVERIFY(accepted.valid);
        const auto rejected = manager.validateConfig(
            pluginId, {{QStringLiteral("prefix"), QStringLiteral("blocked")}});
        QVERIFY(rejected.supported);
        QVERIFY(!rejected.valid);
        QVERIFY(!rejected.error.isEmpty());
        const QVariantMap invoked = actions.invoke(pluginId, QStringLiteral("refresh"));
        QVERIFY(invoked.value(QStringLiteral("accepted")).toBool());
        QVERIFY(invoked.value(QStringLiteral("pending")).toBool());
        const QVariantMap disabled = actions.invoke(pluginId, QStringLiteral("disabled"));
        QVERIFY(!disabled.value(QStringLiteral("accepted")).toBool());
        QCOMPARE(disabled.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("action_disabled"));
        manager.shutdownAll();
    }
};

QTEST_GUILESS_MAIN(PluginManagerTest)
#include "PluginManagerTest.moc"
