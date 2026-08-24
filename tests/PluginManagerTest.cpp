// PluginManagerTest.cpp — 原生与 host 子插件单实例生命周期回归测试
#include "ConfigService.h"
#include "PluginActionRegistry.h"
#include "PluginManager.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
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

bool writeJson(const QString& path, const QJsonObject& object) {
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath())
        && file.open(QIODevice::WriteOnly)
        && file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0;
}

QString prepareManagedPayload(const QString& configDirectory,
                              const QString& id,
                              const QString& version,
                              const QString& runtime) {
    const QString versionDirectory = QDir(configDirectory).filePath(
        QStringLiteral("plugin-store/%1/versions/%2").arg(id, version));
    const QString payload = QDir(versionDirectory).filePath(QStringLiteral("payload"));
    if (!QDir().mkpath(payload)) return {};
    if (!writeJson(QDir(configDirectory).filePath(
            QStringLiteral("plugin-store/%1/current.json").arg(id)),
        {{QStringLiteral("schemaVersion"), 1},
         {QStringLiteral("version"), version}})) {
        return {};
    }
    if (!writeJson(QDir(versionDirectory).filePath(QStringLiteral("install.json")),
        {{QStringLiteral("schemaVersion"), 1},
         {QStringLiteral("id"), id},
         {QStringLiteral("version"), version},
         {QStringLiteral("runtime"), runtime},
         {QStringLiteral("source"), QStringLiteral("test")}})) {
        return {};
    }
    return payload;
}
} // namespace

class PluginManagerTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        // 测试宿主版本必须跟随根 CMake 唯一版本源，避免发布升级后把内置 V2 插件误判为不兼容。
        QCoreApplication::setApplicationVersion(QStringLiteral(XUANHUB_VERSION));
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

    void managedNativeVersionShadowsBuiltInPlugin() {
        const QString sourceRoot = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        const QString builtIn = QDir(sourceRoot).filePath(QStringLiteral("clock"));
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const QString payload = prepareManagedPayload(
            config.path(), QStringLiteral("clock"), QStringLiteral("0.1.0"),
            QStringLiteral("native"));
        QVERIFY(!payload.isEmpty());
        for (const QString& file : {QStringLiteral("clock.dll"),
                                    QStringLiteral("Clock.qml"),
                                    QStringLiteral("metadata.json")}) {
            const QString target = QDir(payload).filePath(file);
            QVERIFY2(QFile::copy(QDir(builtIn).filePath(file), target), qPrintable(file));
            QVERIFY(QFile::setPermissions(
                target, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        }

        xuanhub::EventBus bus;
        PluginManager manager(&bus, config.path());
        manager.loadFrom(sourceRoot, {entry(QStringLiteral("clock"))});
        const auto* loaded = manager.loadedPlugin(QStringLiteral("clock"));
        QVERIFY2(loaded, qPrintable(manager.loadError(QStringLiteral("clock"))));
        QVERIFY(QFileInfo(loaded->qmlUrl.toLocalFile()).absoluteFilePath()
                    .startsWith(QFileInfo(payload).absoluteFilePath(),
                                Qt::CaseInsensitive));
        manager.shutdownAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QString managedLibrary =
            QDir(payload).filePath(QStringLiteral("clock.dll"));
        bool removed = false;
        for (int attempt = 0; attempt < 20 && !removed; ++attempt) {
            QTest::qWait(50);
            removed = QFile::remove(managedLibrary);
        }
        QVERIFY2(removed,
                 "受管插件 DLL 在 shutdownAll 后仍被进程占用");
    }

    void managedPythonVersionShadowsBuiltInPlugin() {
        const QString sourceRoot = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        const QString builtIn = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("plugins_py/hello"));
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const QString payload = prepareManagedPayload(
            config.path(), QStringLiteral("py_hello"), QStringLiteral("0.1.0"),
            QStringLiteral("python"));
        QVERIFY(!payload.isEmpty());
        for (const QString& file : {QStringLiteral("main.py"),
                                    QStringLiteral("Hello.qml"),
                                    QStringLiteral("plugin.json")}) {
            QVERIFY2(QFile::copy(QDir(builtIn).filePath(file),
                                 QDir(payload).filePath(file)), qPrintable(file));
            QVERIFY(QFile::setPermissions(QDir(payload).filePath(file),
                QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        }

        xuanhub::EventBus bus;
        PluginManager manager(&bus, config.path());
        manager.loadFrom(sourceRoot,
                         {entry(QStringLiteral("pybridge")),
                          entry(QStringLiteral("py_hello"))});
        const auto* loaded = manager.loadedPlugin(QStringLiteral("py_hello"));
        QVERIFY2(loaded, qPrintable(manager.loadError(QStringLiteral("py_hello"))));
        QVERIFY(QFileInfo(loaded->qmlUrl.toLocalFile()).absoluteFilePath()
                    .startsWith(QFileInfo(payload).absoluteFilePath(),
                                Qt::CaseInsensitive));
        manager.shutdownAll();
    }

    void escapingPythonManifestDoesNotExecuteWorker() {
        QTemporaryDir configRoot;
        QVERIFY(configRoot.isValid());
        const QString pluginId = QStringLiteral("py_escape_manifest_test");
        const QString payload = prepareManagedPayload(
            configRoot.path(), pluginId, QStringLiteral("1.0.0"),
            QStringLiteral("python"));
        QVERIFY(!payload.isEmpty());
        const QDir pluginDirectory(payload);
        const QString outsideQml = QDir(payload).absoluteFilePath(
            QStringLiteral("../escape-test.qml"));
        QFile qml(outsideQml);
        QVERIFY(qml.open(QIODevice::WriteOnly));
        QVERIFY(qml.write("import QtQuick\nItem {}") > 0);
        qml.close();

        QFile python(pluginDirectory.filePath(QStringLiteral("main.py")));
        QVERIFY(python.open(QIODevice::WriteOnly));
        QVERIFY(python.write(
            "from pathlib import Path\n"
            "Path(__file__).with_name('executed').write_text('bad')\n"
            "def update(): return {}\n") > 0);
        python.close();
        const QJsonObject object{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Escape Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("qml"), QStringLiteral("../escape-test.qml")}};
        QFile manifest(pluginDirectory.filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QVERIFY(manifest.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0);
        manifest.close();

        const QString pluginsDir = qEnvironmentVariable("XUANHUB_TEST_PLUGINS");
        xuanhub::EventBus bus;
        PluginManager manager(&bus, configRoot.path());
        manager.loadFrom(pluginsDir,
                         {entry(QStringLiteral("pybridge")), entry(pluginId)});
        QVERIFY(!manager.loadedPlugin(pluginId));
        QVERIFY(!manager.loadError(pluginId).isEmpty());
        QVERIFY(!QFileInfo(pluginDirectory.filePath(
                              QStringLiteral("executed"))).exists());
        manager.shutdownAll();
        QVERIFY(QFile::remove(outsideQml));
    }

    void missingPythonDependencyIsReportedBeforeWorkerStarts() {
        QTemporaryDir configRoot;
        QVERIFY(configRoot.isValid());
        const QString pluginId = QStringLiteral("py_doctor_missing_test");
        const QString payload = prepareManagedPayload(
            configRoot.path(), pluginId, QStringLiteral("1.0.0"),
            QStringLiteral("python"));
        QVERIFY(!payload.isEmpty());
        const QDir pluginDirectory(payload);

        QFile python(pluginDirectory.filePath(QStringLiteral("main.py")));
        QVERIFY(python.open(QIODevice::WriteOnly));
        QVERIFY(python.write(
            "from pathlib import Path\n"
            "Path(__file__).with_name('executed').write_text('bad')\n"
            "def update(): return {}\n") > 0);
        python.close();
        QFile qml(pluginDirectory.filePath(QStringLiteral("Doctor.qml")));
        QVERIFY(qml.open(QIODevice::WriteOnly));
        QVERIFY(qml.write("import QtQuick\nItem { property var plugin }") > 0);
        qml.close();
        const QJsonObject manifestObject{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Doctor Missing Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("qml"), QStringLiteral("Doctor.qml")},
            {QStringLiteral("python"), QJsonObject{
                {QStringLiteral("version"), QStringLiteral(">=3.12")},
                {QStringLiteral("packages"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"),
                     QStringLiteral("xuanhub-package-that-does-not-exist")},
                    {QStringLiteral("import"), QStringLiteral("xuanhub_missing")},
                    {QStringLiteral("version"), QStringLiteral(">=1.0")},
                }}},
            }},
        };
        QFile manifest(pluginDirectory.filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QVERIFY(manifest.write(QJsonDocument(manifestObject).toJson(
            QJsonDocument::Compact)) > 0);
        manifest.close();

        xuanhub::EventBus bus;
        PluginManager manager(&bus, configRoot.path());
        manager.loadFrom(qEnvironmentVariable("XUANHUB_TEST_PLUGINS"),
                         {entry(QStringLiteral("pybridge")), entry(pluginId)});
        QVERIFY(!manager.loadedPlugin(pluginId));
        const QString loadError = manager.loadError(pluginId);
        QVERIFY2(loadError.contains(QStringLiteral("xuanhub-package-that-does-not-exist")),
                 qPrintable(loadError));
        QVERIFY2(loadError.contains(QStringLiteral("wheel")), qPrintable(loadError));
        QVERIFY(!QFileInfo(pluginDirectory.filePath(
                              QStringLiteral("executed"))).exists());
        manager.shutdownAll();
    }

    void privatePythonEnvironmentRequiresExplicitInstall() {
        QTemporaryDir configRoot;
        QVERIFY(configRoot.isValid());
        const QString pluginId = QStringLiteral("py_private_environment_test");
        const QString payload = prepareManagedPayload(
            configRoot.path(), pluginId, QStringLiteral("1.0.0"),
            QStringLiteral("python"));
        QVERIFY(!payload.isEmpty());
        const QDir pluginDirectory(payload);
        QVERIFY(pluginDirectory.mkpath(QStringLiteral("wheels")));
        QFile wheel(pluginDirectory.filePath(
            QStringLiteral("wheels/demo-1.0.0-py3-none-any.whl")));
        QVERIFY(wheel.open(QIODevice::WriteOnly));
        QVERIFY(wheel.write("not installed without confirmation") > 0);
        wheel.close();
        QFile main(pluginDirectory.filePath(QStringLiteral("main.py")));
        QVERIFY(main.open(QIODevice::WriteOnly));
        QVERIFY(main.write("def update(): return {}\n") > 0);
        main.close();
        QFile qml(pluginDirectory.filePath(QStringLiteral("Private.qml")));
        QVERIFY(qml.open(QIODevice::WriteOnly));
        QVERIFY(qml.write("import QtQuick\nItem { property var plugin }") > 0);
        qml.close();
        const QJsonObject object{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Private Environment Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("qml"), QStringLiteral("Private.qml")},
            {QStringLiteral("python"), QJsonObject{
                {QStringLiteral("packages"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("demo")},
                    {QStringLiteral("version"), QStringLiteral("==1.0.0")}}}},
                {QStringLiteral("environment"), QJsonObject{
                    {QStringLiteral("mode"), QStringLiteral("private")},
                    {QStringLiteral("wheels"), QStringLiteral("wheels")}}},
            }},
        };
        QFile manifest(pluginDirectory.filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QVERIFY(manifest.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0);
        manifest.close();

        xuanhub::EventBus bus;
        PluginManager manager(&bus, configRoot.path());
        manager.loadFrom(qEnvironmentVariable("XUANHUB_TEST_PLUGINS"),
                         {entry(QStringLiteral("pybridge")), entry(pluginId)});
        QVERIFY(!manager.loadedPlugin(pluginId));
        const QString privateError = manager.loadError(pluginId);
        QVERIFY2(privateError.contains(QStringLiteral("private")),
                 qPrintable(privateError));
        const QVariantMap plan = manager.dependencyInstallPlan(pluginId);
        QVERIFY(plan.value(QStringLiteral("available")).toBool());
        QVERIFY(plan.value(QStringLiteral("requiresConfirmation")).toBool());
        QCOMPARE(plan.value(QStringLiteral("packages")).toStringList(),
                 QStringList{QStringLiteral("demo")});
        QCOMPARE(plan.value(QStringLiteral("wheelCount")).toInt(), 1);
        QVERIFY(!plan.contains(QStringLiteral("_environment")));
        QVERIFY(!QDir(configRoot.path()).exists(
            QStringLiteral("python-envs/") + pluginId));
        manager.shutdownAll();
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
        const auto menuConfigValid = manager.validateConfig(
            menu.id,
            {{QStringLiteral("pluginActionsEnabled"), true},
             {QStringLiteral("disabledPluginActions"), QVariantMap{
                 {QStringLiteral("sysinfo"),
                  QVariantList{QStringLiteral("refresh")}}}}});
        QVERIFY(menuConfigValid.supported);
        QVERIFY(menuConfigValid.valid);
        const auto menuConfigInvalid = manager.validateConfig(
            menu.id,
            {{QStringLiteral("disabledPluginActions"), QStringLiteral("invalid")}});
        QVERIFY(menuConfigInvalid.supported);
        QVERIFY(!menuConfigInvalid.valid);

        PluginConfigEntry filteredMenu = menu;
        filteredMenu.config = {
            {QStringLiteral("pluginActionsEnabled"), true},
            {QStringLiteral("disabledPluginActions"), QVariantMap{
                {QStringLiteral("sysinfo"),
                 QVariantList{QStringLiteral("refresh")}}}},
        };
        QCOMPARE(manager.applyConfig(filteredMenu),
                 PluginManager::ConfigApplyResult::Applied);
        QCOMPARE(manager.loadedPlugin(menu.id)->viewModel, menuViewModel);
        bool sysinfoRefreshEnabled = true;
        QVERIFY(QMetaObject::invokeMethod(
            manager.loadedPlugin(menu.id)->viewModel,
            "isPluginActionEnabled", Qt::DirectConnection,
            Q_RETURN_ARG(bool, sysinfoRefreshEnabled),
            Q_ARG(QString, QStringLiteral("sysinfo")),
            Q_ARG(QString, QStringLiteral("refresh"))));
        QVERIFY(!sysinfoRefreshEnabled);

        filteredMenu.config.insert(QStringLiteral("pluginActionsEnabled"), false);
        QCOMPARE(manager.applyConfig(filteredMenu),
                 PluginManager::ConfigApplyResult::Applied);
        QVERIFY(!manager.loadedPlugin(menu.id)->viewModel
                     ->property("pluginActionsEnabled").toBool());
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
        QVERIFY2(manager.loadedPlugin(clock.id), "clock failed to start in test runtime");
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
        QTemporaryDir configRoot;
        QVERIFY(configRoot.isValid());
        const QString pluginId = QStringLiteral("py_migration_test");
        const QString payload = prepareManagedPayload(
            configRoot.path(), pluginId, QStringLiteral("1.0.0"),
            QStringLiteral("python"));
        QVERIFY(!payload.isEmpty());
        const QDir pluginDirectory(payload);

        QFile qml(pluginDirectory.filePath(QStringLiteral("Migration.qml")));
        QVERIFY(qml.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(qml.write("import QtQuick\nItem { property var plugin }") > 0);
        qml.close();
        QFile python(pluginDirectory.filePath(QStringLiteral("main.py")));
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
        QFile manifest(pluginDirectory.filePath(QStringLiteral("plugin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(manifest.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), pluginId},
            {QStringLiteral("name"), QStringLiteral("Migration Test")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("configVersion"), 2},
            {QStringLiteral("qml"), QStringLiteral("Migration.qml")},
            {QStringLiteral("layout"), QJsonObject{
                {QStringLiteral("region"), QStringLiteral("center")},
                {QStringLiteral("minWidth"), 40},
                {QStringLiteral("preferredWidth"), 120},
                {QStringLiteral("priority"), 75},
                {QStringLiteral("overflow"), QStringLiteral("menu")}}},
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
        PluginManager manager(&bus, configRoot.path());
        PluginConfigEntry child = entry(pluginId);
        child.position.clear();
        child.configVersion = 1;
        manager.loadFrom(pluginsDir,
                         {entry(QStringLiteral("pybridge")), child});
        QVERIFY(manager.loadedPlugin(pluginId));
        QCOMPARE(manager.loadedPlugin(pluginId)->actions.size(), 2);
        QCOMPARE(manager.loadedPlugin(pluginId)->actions.first().id,
                 QStringLiteral("refresh"));
        QVERIFY(manager.loadedPlugin(pluginId)->layout.declared);
        QCOMPARE(manager.loadedPlugin(pluginId)->position, QStringLiteral("center"));
        QCOMPARE(manager.loadedPlugin(pluginId)->layout.preferredWidth, 120);
        QCOMPARE(manager.loadedPlugin(pluginId)->layout.priority, 75);
        QCOMPARE(manager.loadedPlugin(pluginId)->layout.overflow,
                 QStringLiteral("menu"));
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
