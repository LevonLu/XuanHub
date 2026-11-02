// ConfigServiceTest.cpp — 配置根类型、重复插件与 DPAPI 密钥引用回归测试
#include "ConfigService.h"
#include "SecretStore.h"
#include "SettingsPluginModel.h"
#include "SettingsPluginSortModel.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

namespace {
bool writeJson(const QString& directory, const QJsonDocument& doc) {
    QFile file(directory + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(doc.toJson(QJsonDocument::Compact)) >= 0;
}

bool writeJsonObject(const QString& path, const QJsonObject& object) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0;
}

QJsonObject configWithPlugins(const QJsonArray& plugins, int height = 48) {
    return {
        {QStringLiteral("bar"), QJsonObject{{QStringLiteral("height"), height}}},
        {QStringLiteral("plugins"), plugins},
    };
}

QJsonObject plugin(const QString& id, const QJsonObject& config = {}) {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("position"), QStringLiteral("right")},
        {QStringLiteral("config"), config},
    };
}
} // namespace

class ConfigServiceTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void mergesUsageHubWithoutWritingAndProtectsAdoptedAccounts() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store(dir.path());
        const QString oldSecret = SecretStore::idForPluginPointer(QStringLiteral("py_usage_hub"), QStringLiteral("/vendors/stable/apiKey"));
        QVERIFY(store.write(oldSecret, QStringLiteral("fixture-key")));
        QJsonObject vendor{{"id", "deepseek"}, {"secretId", "stable"}, {"mode", "balance"}, {"apiKey", SecretStore::referenceValue(oldSecret)}};
        QJsonObject old = plugin(QStringLiteral("py_usage_hub"), {{"vendors", QJsonArray{vendor}}});
        old["position"] = "left";
        old["note"] = "preserved";
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({old}))));
        const auto before = QJsonDocument(configWithPlugins({old})).toJson(QJsonDocument::Compact);
        ConfigService config;
        QVERIFY(config.load(dir.path(), {}));
        QCOMPARE(config.pluginEntries().size(), 1);
        const auto entry = config.pluginEntries().first();
        QCOMPARE(entry.id, QStringLiteral("py_agent_usage"));
        QCOMPARE(entry.position, QStringLiteral("left"));
        const auto accounts = entry.config.value(QStringLiteral("accounts")).toList();
        QCOMPARE(accounts.size(), 1);
        QCOMPARE(accounts.first().toMap().value(QStringLiteral("credential")).toString(), QStringLiteral("fixture-key"));
        QFile original(dir.filePath(QStringLiteral("config.json")));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), before);
        QVERIFY(config.load(dir.path(), {}));
        QCOMPARE(config.pluginEntries().first().config.value(QStringLiteral("accounts")).toList().size(), 1);

        const auto schema = QJsonDocument::fromJson(R"([{"fields":[{"key":"accounts","type":"array","itemKey":"id","items":{"type":"object","fields":[{"key":"credential","type":"string","secret":true}]}}]}])").array();
        QVERIFY(writeJsonObject(dir.filePath(QStringLiteral("plugins_py/agent_usage/plugin.json")), {{"id", "py_agent_usage"}, {"name", "Token Usage"}, {"apiVersion", 2}, {"settings", schema}}));
        SettingsPluginModel model;
        model.reload(config.pluginEntries(), config.rawConfig().value(QStringLiteral("plugins")).toArray(), dir.path(), config.activePath());
        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        QVERIFY(QJsonDocument(exported).toJson().contains("py_usage_hub"));
        QVERIFY(model.setConfigValue(QStringLiteral("py_agent_usage"), QStringLiteral("legacyImportPending"), false));
        auto transaction = store.beginTransaction();
        QVERIFY2(model.toPluginEntriesJson(&exported, &error, &transaction), qPrintable(error));
        const auto serialized = QJsonDocument(exported).toJson();
        QVERIFY(!serialized.contains("fixture-key"));
        QVERIFY(!serialized.contains("py_usage_hub"));
        QVERIFY(serialized.contains("preserved"));
        QVERIFY(transaction.commit());
        transaction.finalize();
        const QString newSecret = SecretStore::idForPluginPointer(QStringLiteral("py_agent_usage"), QStringLiteral("/accounts/legacy-stable/credential"));
        QCOMPARE(store.read(newSecret).value_or(QString()), QStringLiteral("fixture-key"));
        QVERIFY(store.read(oldSecret).has_value());
    }

    void mergesBothUsagePluginsAndRejectsForeignCredentials() {
        QTemporaryDir dir;
        SecretStore store(dir.path());
        const QString foreign = SecretStore::idForPluginPointer(QStringLiteral("py_other"), QStringLiteral("/vendors/a/apiKey"));
        QVERIFY(store.write(foreign, QStringLiteral("private-fixture")));
        const QJsonObject vendor{{"id", "glm"}, {"secretId", "a"}, {"mode", "plan"}, {"apiKey", SecretStore::referenceValue(foreign)}};
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({plugin(QStringLiteral("py_usage_hub"), {{"vendors", QJsonArray{vendor}}}), plugin(QStringLiteral("py_agent_usage"), {{"period", "30d"}, {"custom", 42}})}))));
        ConfigService config;
        QVERIFY(config.load(dir.path(), {}));
        QCOMPARE(config.pluginEntries().size(), 1);
        const auto entry = config.pluginEntries().first();
        QCOMPARE(entry.config.value(QStringLiteral("period")).toString(), QStringLiteral("30d"));
        QCOMPARE(entry.config.value(QStringLiteral("custom")).toInt(), 42);
        QVERIFY(entry.config.value(QStringLiteral("accounts")).toList().first().toMap().value(QStringLiteral("credential")).toString().isEmpty());
    }

    void settingsQmlFollowsRuntimeManifestEntry_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<int>("apiVersion");
        QTest::addColumn<QString>("expected");
        QTest::newRow("native-v2") << QStringLiteral("dll") << 2 << QStringLiteral("V2Settings.qml");
        QTest::newRow("legacy-native") << QStringLiteral("dll") << 1 << QStringLiteral("TopSettings.qml");
        QTest::newRow("python-v2") << QStringLiteral("python") << 2 << QStringLiteral("TopSettings.qml");
    }

    void settingsQmlFollowsRuntimeManifestEntry() {
        QFETCH(QString, kind);
        QFETCH(int, apiVersion);
        QFETCH(QString, expected);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString folder = kind == QLatin1String("dll")
            ? QStringLiteral("plugins/demo") : QStringLiteral("plugins_py/demo");
        const QString manifestFile = kind == QLatin1String("dll")
            ? QStringLiteral("metadata.json") : QStringLiteral("plugin.json");
        QVERIFY(writeJsonObject(directory.filePath(folder + QLatin1Char('/') + manifestFile), {
            {QStringLiteral("id"), QStringLiteral("demo")},
            {QStringLiteral("apiVersion"), apiVersion},
            {QStringLiteral("settingsQml"), QStringLiteral("TopSettings.qml")},
            {QStringLiteral("entry"), QJsonObject{
                {QStringLiteral("settingsQml"), QStringLiteral("V2Settings.qml")}}},
        }));
        SettingsPluginModel model;
        model.reload({}, {}, directory.path(), directory.filePath(QStringLiteral("config.json")));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), SettingsPluginModel::SettingsQmlRole).toUrl(),
                 QUrl::fromLocalFile(directory.filePath(folder + QLatin1Char('/') + expected)));
    }

    void distinguishesSavedPendingAndFailedRuntimeConfig() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeJson(directory.path(), QJsonDocument(QJsonObject{})));
        ConfigService service;
        QVERIFY(service.load(directory.path(), directory.path()));
        QVERIFY(service.save(service.rawConfig()));
        QCOMPARE(service.applyState(), QStringLiteral("saved"));
        service.updatePluginApplyStates({});
        QCOMPARE(service.applyState(), QStringLiteral("saved"));
        service.beginRuntimeApply();
        service.updatePluginApplyStates({{QStringLiteral("py_test"),
            QVariantMap{{QStringLiteral("state"), QStringLiteral("pending")}}}});
        service.finishRuntimeApply();
        QCOMPARE(service.applyState(), QStringLiteral("pending"));
        service.updatePluginApplyStates({{QStringLiteral("py_test"),
            QVariantMap{{QStringLiteral("state"), QStringLiteral("failed")},
                        {QStringLiteral("error"), QStringLiteral("timeout")}}}});
        QCOMPARE(service.applyState(), QStringLiteral("failed"));
        service.updatePluginApplyStates({});
        QCOMPARE(service.applyState(), QStringLiteral("applied"));
    }
    void managedPluginCatalogOverridesBuiltInMetadata() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({}))));

        const QJsonObject builtIn{
            {QStringLiteral("id"), QStringLiteral("demo")},
            {QStringLiteral("name"), QStringLiteral("Built-in")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
        };
        QVERIFY(writeJsonObject(
            dir.filePath(QStringLiteral("plugins/demo/metadata.json")), builtIn));

        const QString nativeVersion = dir.filePath(
            QStringLiteral("plugin-store/demo/versions/2.0.0"));
        QVERIFY(writeJsonObject(
            dir.filePath(QStringLiteral("plugin-store/demo/current.json")),
            {{QStringLiteral("schemaVersion"), 1},
             {QStringLiteral("version"), QStringLiteral("2.0.0")}}));
        QVERIFY(writeJsonObject(QDir(nativeVersion).filePath(QStringLiteral("install.json")),
            {{QStringLiteral("schemaVersion"), 1},
             {QStringLiteral("id"), QStringLiteral("demo")},
             {QStringLiteral("version"), QStringLiteral("2.0.0")},
             {QStringLiteral("runtime"), QStringLiteral("native")}}));
        QVERIFY(writeJsonObject(
            QDir(nativeVersion).filePath(QStringLiteral("payload/metadata.json")),
            {{QStringLiteral("id"), QStringLiteral("demo")},
             {QStringLiteral("name"), QStringLiteral("Managed")},
             {QStringLiteral("version"), QStringLiteral("2.0.0")},
             {QStringLiteral("apiVersion"), 2}}));

        const QString pythonVersion = dir.filePath(
            QStringLiteral("plugin-store/py_managed/versions/3.0.0"));
        QVERIFY(writeJsonObject(
            dir.filePath(QStringLiteral("plugin-store/py_managed/current.json")),
            {{QStringLiteral("schemaVersion"), 1},
             {QStringLiteral("version"), QStringLiteral("3.0.0")}}));
        QVERIFY(writeJsonObject(QDir(pythonVersion).filePath(QStringLiteral("install.json")),
            {{QStringLiteral("schemaVersion"), 1},
             {QStringLiteral("id"), QStringLiteral("py_managed")},
             {QStringLiteral("version"), QStringLiteral("3.0.0")},
             {QStringLiteral("runtime"), QStringLiteral("python")}}));
        QVERIFY(writeJsonObject(
            QDir(pythonVersion).filePath(QStringLiteral("payload/plugin.json")),
            {{QStringLiteral("id"), QStringLiteral("py_managed")},
             {QStringLiteral("name"), QStringLiteral("Managed Python")},
             {QStringLiteral("version"), QStringLiteral("3.0.0")}}));

        SettingsPluginModel model;
        model.reload({}, {}, dir.path(), dir.filePath(QStringLiteral("config.json")));
        auto rowFor = [&model](const QString& id) {
            for (int row = 0; row < model.rowCount(); ++row) {
                if (model.data(model.index(row), SettingsPluginModel::IdRole).toString() == id)
                    return row;
            }
            return -1;
        };
        const int nativeRow = rowFor(QStringLiteral("demo"));
        QVERIFY(nativeRow >= 0);
        QCOMPARE(model.data(model.index(nativeRow), SettingsPluginModel::VersionRole).toString(),
                 QStringLiteral("2.0.0"));
        QCOMPARE(model.data(model.index(nativeRow), SettingsPluginModel::NameRole).toString(),
                 QStringLiteral("Managed"));
        const int pythonRow = rowFor(QStringLiteral("py_managed"));
        QVERIFY(pythonRow >= 0);
        QCOMPARE(model.data(model.index(pythonRow), SettingsPluginModel::KindRole).toString(),
                 QStringLiteral("python"));
        QCOMPARE(model.data(model.index(pythonRow), SettingsPluginModel::VersionRole).toString(),
                 QStringLiteral("3.0.0"));
    }

    void pluginConfigVersionDefaultsAndIsPreservedBySettingsDraft() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject versioned = plugin(QStringLiteral("versioned"));
        versioned[QStringLiteral("configVersion")] = 3;
        QJsonObject malformed = plugin(QStringLiteral("malformed"));
        malformed[QStringLiteral("configVersion")] = 0;
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins(
            {plugin(QStringLiteral("legacy")), versioned, malformed}))));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QCOMPARE(service.pluginEntries().size(), 3);
        QCOMPARE(service.pluginEntries().at(0).configVersion, 1);
        QCOMPARE(service.pluginEntries().at(1).configVersion, 3);
        QCOMPARE(service.pluginEntries().at(2).configVersion, 1);

        const QString sidecarDir = dir.filePath(QStringLiteral("plugins/versioned"));
        QVERIFY(QDir().mkpath(sidecarDir));
        QFile sidecar(sidecarDir + QStringLiteral("/metadata.json"));
        QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sidecar.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("versioned")},
            {QStringLiteral("configVersion"), 5},
        }).toJson(QJsonDocument::Compact));
        sidecar.close();

        SettingsPluginModel model;
        model.reload(service.pluginEntries(), service.rawConfig()
                         .value(QStringLiteral("plugins")).toArray(),
                     dir.path(), dir.filePath(QStringLiteral("config.json")));
        int versionedRow = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row), SettingsPluginModel::IdRole).toString()
                == QStringLiteral("versioned")) {
                versionedRow = row;
                break;
            }
        }
        QVERIFY(versionedRow >= 0);
        QCOMPARE(model.data(model.index(versionedRow),
                            SettingsPluginModel::ConfigVersionRole).toInt(), 3);
        QCOMPARE(model.data(model.index(versionedRow),
                            SettingsPluginModel::TargetConfigVersionRole).toInt(), 5);
        QVERIFY(!model.data(model.index(versionedRow),
                            SettingsPluginModel::MigrationAvailableRole).toBool());

        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        for (const QJsonValue& value : exported) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("id")).toString() == QStringLiteral("versioned"))
                QCOMPARE(object.value(QStringLiteral("configVersion")).toInt(), 3);
        }

        QVERIFY(model.setMigrationSuggestion(
            QStringLiteral("versioned"), 5,
            {{QStringLiteral("migrated"), true}}, {}));
        QVERIFY(model.data(model.index(versionedRow),
                           SettingsPluginModel::MigrationAvailableRole).toBool());
        QVERIFY(!model.isDirty());
        QVERIFY(model.acceptMigration(QStringLiteral("versioned")));
        QVERIFY(model.isDirty());
        exported = {};
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        for (const QJsonValue& value : exported) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("id")).toString() != QStringLiteral("versioned"))
                continue;
            QCOMPARE(object.value(QStringLiteral("configVersion")).toInt(), 5);
            QCOMPARE(object.value(QStringLiteral("config")).toObject()
                         .value(QStringLiteral("migrated")).toBool(), true);
        }
    }

    void nonObjectRootKeepsCurrentState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({plugin(QStringLiteral("clock"))}, 52))));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QCOMPARE(service.barHeight(), 52);
        QCOMPARE(service.pluginEntries().size(), 1);

        QVERIFY(writeJson(dir.path(), QJsonDocument(QJsonArray{})));
        QVERIFY(!service.load(dir.path(), dir.path()));
        QCOMPARE(service.barHeight(), 52);
        QCOMPARE(service.pluginEntries().size(), 1);
    }

    void duplicatePluginIdKeepsCurrentState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({plugin(QStringLiteral("clock"))}, 44))));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({
            plugin(QStringLiteral("clock")), plugin(QStringLiteral("clock"))}, 72))));

        QVERIFY(!service.load(dir.path(), dir.path()));
        QCOMPARE(service.barHeight(), 44);
        QCOMPARE(service.pluginEntries().size(), 1);
    }

    void languageDefaultsValidatesAndPreservesUnknownFields() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root = configWithPlugins({});
        root[QStringLiteral("futureRoot")] = 17;
        root[QStringLiteral("ui")] = QJsonObject{
            {QStringLiteral("language"), QStringLiteral("xx-INVALID")},
            {QStringLiteral("futureUi"), QStringLiteral("kept")}};
        QVERIFY(writeJson(dir.path(), QJsonDocument(root)));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QCOMPARE(service.language(), QStringLiteral("system"));
        QJsonObject saved = service.rawConfig();
        QJsonObject ui = saved.value(QStringLiteral("ui")).toObject();
        ui[QStringLiteral("language")] = QStringLiteral("en-US");
        saved[QStringLiteral("ui")] = ui;
        QVERIFY(service.save(saved));

        QFile file(dir.filePath(QStringLiteral("config.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject reparsed = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(reparsed.value(QStringLiteral("futureRoot")).toInt(), 17);
        QCOMPARE(reparsed.value(QStringLiteral("ui")).toObject()
                     .value(QStringLiteral("futureUi")).toString(), QStringLiteral("kept"));
        QCOMPARE(reparsed.value(QStringLiteral("ui")).toObject()
                     .value(QStringLiteral("language")).toString(), QStringLiteral("en-US"));

        QTemporaryDir missingUiDir;
        QVERIFY(writeJson(missingUiDir.path(), QJsonDocument(configWithPlugins({}))));
        ConfigService missingUi;
        QVERIFY(missingUi.load(missingUiDir.path(), missingUiDir.path()));
        QCOMPARE(missingUi.language(), QStringLiteral("system"));
    }

    void schemaEnumFormatsUseStableValues() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pluginDir = dir.filePath(QStringLiteral("plugins_py/py_enum"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile metadata(pluginDir + QStringLiteral("/plugin.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        const QJsonObject schemaPlugin{
            {QStringLiteral("id"), QStringLiteral("py_enum")},
            {QStringLiteral("name"), QStringLiteral("枚举测试")},
            {QStringLiteral("settings"), QJsonArray{
                QJsonObject{{QStringLiteral("group"), QStringLiteral("常规")},
                            {QStringLiteral("fields"), QJsonArray{
                                QJsonObject{{QStringLiteral("key"), QStringLiteral("legacy")},
                                            {QStringLiteral("type"), QStringLiteral("enum")},
                                            {QStringLiteral("options"), QJsonArray{
                                                QStringLiteral("one"), QStringLiteral("two")}}},
                                QJsonObject{{QStringLiteral("key"), QStringLiteral("stable")},
                                            {QStringLiteral("type"), QStringLiteral("enum")},
                                            {QStringLiteral("options"), QJsonArray{
                                                QJsonObject{{QStringLiteral("value"), QStringLiteral("daily")},
                                                            {QStringLiteral("label"), QStringLiteral("每日重复")}}}}}
                            }}}
            }}
        };
        metadata.write(QJsonDocument(schemaPlugin).toJson(QJsonDocument::Compact));
        metadata.close();

        PluginConfigEntry enumEntry;
        enumEntry.id = QStringLiteral("py_enum");
        enumEntry.config = {{QStringLiteral("legacy"), QStringLiteral("one")},
                            {QStringLiteral("stable"), QStringLiteral("daily")}};

        SettingsPluginModel model;
        model.reload({enumEntry}, {}, dir.path(),
                     dir.filePath(QStringLiteral("config.json")));
        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        int enumRow = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row), SettingsPluginModel::IdRole).toString()
                == QStringLiteral("py_enum")) enumRow = row;
        }
        QVERIFY(enumRow >= 0);
        const QJsonArray schema = model.data(model.index(enumRow), SettingsPluginModel::SchemaRole)
                                      .toJsonArray();
        const QJsonArray fields = schema.first().toObject().value(QStringLiteral("fields")).toArray();
        QVERIFY(fields.at(0).toObject().value(QStringLiteral("options")).toArray().first().isObject());
        QCOMPARE(fields.at(1).toObject().value(QStringLiteral("options")).toArray().first()
                     .toObject().value(QStringLiteral("value")).toString(), QStringLiteral("daily"));
    }

    void schemaExtendedTypesAndRegexAreValidated() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pluginDir = dir.filePath(QStringLiteral("plugins_py/py_schema_v2"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile metadata(pluginDir + QStringLiteral("/plugin.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("py_schema_v2")},
            {QStringLiteral("name"), QStringLiteral("Schema V2")},
            {QStringLiteral("settings"), QJsonArray{QJsonObject{
                {QStringLiteral("group"), QStringLiteral("常规")},
                {QStringLiteral("fields"), QJsonArray{
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("slug")},
                                {QStringLiteral("label"), QStringLiteral("标识")},
                                {QStringLiteral("type"), QStringLiteral("string")},
                                {QStringLiteral("regex"), QStringLiteral("^[a-z]+$")}},
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("mapping")},
                                {QStringLiteral("label"), QStringLiteral("映射")},
                                {QStringLiteral("type"), QStringLiteral("object")}},
                    QJsonObject{{QStringLiteral("key"), QStringLiteral("items")},
                                {QStringLiteral("label"), QStringLiteral("项目")},
                                {QStringLiteral("type"), QStringLiteral("array")}}
                }}
            }}}
        }).toJson(QJsonDocument::Compact));
        metadata.close();

        PluginConfigEntry entry;
        entry.id = QStringLiteral("py_schema_v2");
        entry.config = {{QStringLiteral("slug"), QStringLiteral("valid")},
                        {QStringLiteral("mapping"), QVariantMap{{QStringLiteral("x"), 1}}},
                        {QStringLiteral("items"), QVariantList{1, 2}}};
        SettingsPluginModel model;
        model.reload({entry}, {}, dir.path(), dir.filePath(QStringLiteral("config.json")));
        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));

        QVERIFY(model.setConfigValue(entry.id, QStringLiteral("slug"), QStringLiteral("Invalid-1")));
        QVERIFY(!model.toPluginEntriesJson(&exported, &error));
        QVERIFY(error.contains(QStringLiteral("标识")));
        QVERIFY(model.setConfigValue(entry.id, QStringLiteral("slug"), QStringLiteral("valid")));

        QVERIFY(model.setConfigValue(entry.id, QStringLiteral("mapping"), QVariantList{1}));
        QVERIFY(!model.toPluginEntriesJson(&exported, &error));
        QVERIFY(error.contains(QStringLiteral("JSON 对象")));
        QVERIFY(model.setConfigValue(entry.id, QStringLiteral("mapping"),
                                     QVariantMap{{QStringLiteral("x"), 1}}));

        QVERIFY(model.setConfigValue(entry.id, QStringLiteral("items"),
                                     QVariantMap{{QStringLiteral("x"), 1}}));
        QVERIFY(!model.toPluginEntriesJson(&exported, &error));
        QVERIFY(error.contains(QStringLiteral("JSON 数组")));
    }

    void pluginPermissionRolesDistinguishTrustedAndIsolatedRuntimes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const auto writeSidecar = [&](const QString& relativePath,
                                      const QJsonObject& object) {
            const QString path = dir.filePath(relativePath);
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0,
                     true);
        };
        writeSidecar(QStringLiteral("plugins/native_perm/metadata.json"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("native_perm")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("barItem")}},
            {QStringLiteral("permissions"),
             QJsonArray{QStringLiteral("notifications"), QStringLiteral("native.direct")}},
            {QStringLiteral("layout"), QJsonObject{
                {QStringLiteral("minWidth"), 48},
                {QStringLiteral("priority"), 70},
                {QStringLiteral("overflow"), QStringLiteral("menu")}}},
        });
        writeSidecar(QStringLiteral("plugins_py/py_perm/plugin.json"), QJsonObject{
            {QStringLiteral("id"), QStringLiteral("py_perm")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("capabilities"),
             QJsonArray{QStringLiteral("barItem"), QStringLiteral("popup")}},
            {QStringLiteral("permissions"),
             QJsonArray{QStringLiteral("storage.private"), QStringLiteral("network.any")}},
        });

        SettingsPluginModel model;
        model.reload({}, {}, dir.path(), dir.filePath(QStringLiteral("config.json")));
        const auto rowForId = [&](const QString& id) {
            for (int row = 0; row < model.rowCount(); ++row) {
                if (model.data(model.index(row), SettingsPluginModel::IdRole).toString() == id)
                    return row;
            }
            return -1;
        };

        const int nativeRow = rowForId(QStringLiteral("native_perm"));
        QVERIFY(nativeRow >= 0);
        QCOMPARE(model.data(model.index(nativeRow), SettingsPluginModel::ApiVersionRole).toInt(), 2);
        QCOMPARE(model.data(model.index(nativeRow), SettingsPluginModel::TrustLevelRole).toString(),
                 QStringLiteral("trusted-in-process"));
        QCOMPARE(model.data(model.index(nativeRow),
                            SettingsPluginModel::DeclaredPermissionsRole).toStringList(),
                 QStringList({QStringLiteral("notifications"), QStringLiteral("native.direct")}));
        QCOMPARE(model.data(model.index(nativeRow),
                            SettingsPluginModel::GrantedPermissionsRole).toStringList(),
                 QStringList({QStringLiteral("notifications"), QStringLiteral("native.direct")}));
        const QVariantMap layout = model.data(
            model.index(nativeRow), SettingsPluginModel::LayoutPreferenceRole).toMap();
        QCOMPARE(layout.value(QStringLiteral("minWidth")).toInt(), 48);
        QCOMPARE(layout.value(QStringLiteral("priority")).toInt(), 70);
        QCOMPARE(layout.value(QStringLiteral("overflow")).toString(),
                 QStringLiteral("menu"));

        const int pythonRow = rowForId(QStringLiteral("py_perm"));
        QVERIFY(pythonRow >= 0);
        QCOMPARE(model.data(model.index(pythonRow), SettingsPluginModel::TrustLevelRole).toString(),
                 QStringLiteral("isolated"));
        QCOMPARE(model.data(model.index(pythonRow),
                            SettingsPluginModel::CapabilitiesRole).toStringList(),
                 QStringList({QStringLiteral("barItem"), QStringLiteral("popup")}));
        QCOMPARE(model.data(model.index(pythonRow),
                            SettingsPluginModel::GrantedPermissionsRole).toStringList(),
                 QStringList({QStringLiteral("storage.private")}));
    }

    void secretReferenceHydratesOnlyForOwningPlugin() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString secretId = SecretStore::idForPluginField(QStringLiteral("py_secret_demo"),
                                                               QStringLiteral("aiApiKey"));
        SecretStore secrets(dir.path());
        QVERIFY(secrets.write(secretId, QStringLiteral("test-secret")));

        const QJsonObject secretConfig{{QStringLiteral("aiApiKey"),
                                        SecretStore::referenceValue(secretId)}};
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({
            plugin(QStringLiteral("py_secret_demo"), secretConfig)}))));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QCOMPARE(service.pluginEntries().first().config.value(QStringLiteral("aiApiKey")).toString(),
                 QStringLiteral("test-secret"));
        QVERIFY(service.rawConfig().value(QStringLiteral("plugins")).toArray().first().toObject()
                    .value(QStringLiteral("config")).toObject().value(QStringLiteral("aiApiKey")).isObject());
    }

    void secretTransactionCanRollbackAndFinalize() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString secretId = SecretStore::idForPluginField(
            QStringLiteral("py_secret_demo"), QStringLiteral("token"));
        SecretStore store(dir.path());
        QVERIFY(store.write(secretId, QStringLiteral("old")));

        {
            auto transaction = store.beginTransaction();
            QVERIFY(transaction.stageWrite(secretId, QStringLiteral("temporary")));
            QVERIFY(transaction.commit());
            QCOMPARE(store.read(secretId).value_or({}), QStringLiteral("temporary"));
            QVERIFY(transaction.rollback());
        }
        QCOMPARE(store.read(secretId).value_or({}), QStringLiteral("old"));

        {
            auto transaction = store.beginTransaction();
            QVERIFY(transaction.stageWrite(secretId, QStringLiteral("final")));
            QVERIFY(transaction.commit());
            transaction.finalize();
        }
        QCOMPARE(store.read(secretId).value_or({}), QStringLiteral("final"));

        const QString freshDir = dir.filePath(QStringLiteral("fresh"));
        QVERIFY(QDir().mkpath(freshDir));
        {
            SecretStore fresh(freshDir);
            auto transaction = fresh.beginTransaction();
            QVERIFY(transaction.stageWrite(secretId, QStringLiteral("discarded")));
            QVERIFY(transaction.commit());
            // 未 finalize：析构自动恢复为“文件原本不存在”。
        }
        QVERIFY(!QFileInfo::exists(QDir(freshDir).filePath(QStringLiteral("secrets.json"))));
    }

    void usageHubVendorSecretMigratesAndHydrates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonArray settings{QJsonObject{
            {QStringLiteral("group"), QStringLiteral("账户")},
            {QStringLiteral("fields"), QJsonArray{QJsonObject{
                {QStringLiteral("key"), QStringLiteral("vendors")},
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("itemKey"), QStringLiteral("secretId")},
                {QStringLiteral("items"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("fields"), QJsonArray{QJsonObject{
                        {QStringLiteral("key"), QStringLiteral("apiKey")},
                        {QStringLiteral("type"), QStringLiteral("string")},
                        {QStringLiteral("secret"), true}
                    }}}
                }}
            }}}
        }};
        const QString pluginDir = dir.filePath(QStringLiteral("plugins_py/usage_hub"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile sidecar(QDir(pluginDir).filePath(QStringLiteral("plugin.json")));
        QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(sidecar.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("py_nested_secret_fixture")},
            {QStringLiteral("settings"), settings}
        }).toJson(QJsonDocument::Compact)) > 0);
        sidecar.close();
        const QJsonObject vendor{{QStringLiteral("id"), QStringLiteral("deepseek")},
                                 {QStringLiteral("name"), QStringLiteral("DeepSeek")},
                                 {QStringLiteral("apiKey"), QStringLiteral("plain-key")}};
        const QJsonObject vendor2{{QStringLiteral("id"), QStringLiteral("glm")},
                                  {QStringLiteral("name"), QStringLiteral("GLM")},
                                  {QStringLiteral("apiKey"), QStringLiteral("second-key")}};
        const QJsonObject rawEntry = plugin(QStringLiteral("py_nested_secret_fixture"),
                                            {{QStringLiteral("vendors"),
                                              QJsonArray{vendor, vendor2}}});
        PluginConfigEntry entry;
        entry.id = QStringLiteral("py_nested_secret_fixture");
        entry.position = QStringLiteral("right");
        entry.config = rawEntry.value(QStringLiteral("config")).toObject().toVariantMap();

        SettingsPluginModel model;
        model.reload({entry}, QJsonArray{rawEntry}, dir.path(),
                     dir.filePath(QStringLiteral("config.json")));
        QJsonArray exported;
        QString error;
        SecretStore store(dir.path());
        auto transaction = store.beginTransaction();
        QVERIFY2(model.toPluginEntriesJson(&exported, &error, &transaction), qPrintable(error));

        const QJsonObject saved = exported.first().toObject();
        const QJsonArray savedVendors = saved.value(QStringLiteral("config")).toObject()
                                            .value(QStringLiteral("vendors")).toArray();
        const QJsonObject savedVendor = savedVendors.first().toObject();
        const QJsonObject savedVendor2 = savedVendors.at(1).toObject();
        const QString token = savedVendor.value(QStringLiteral("secretId")).toString();
        const QString token2 = savedVendor2.value(QStringLiteral("secretId")).toString();
        QVERIFY(!token.isEmpty());
        QVERIFY(!token2.isEmpty());
        QVERIFY(token != token2);
        const QString secretId = SecretStore::idForPluginPointer(
            QStringLiteral("py_nested_secret_fixture"),
            QStringLiteral("/vendors/%1/apiKey").arg(token));
        const QString secretId2 = SecretStore::idForPluginPointer(
            QStringLiteral("py_nested_secret_fixture"),
            QStringLiteral("/vendors/%1/apiKey").arg(token2));
        QCOMPARE(SecretStore::referenceId(savedVendor.value(QStringLiteral("apiKey"))), secretId);
        QCOMPARE(SecretStore::referenceId(savedVendor2.value(QStringLiteral("apiKey"))), secretId2);
        QVERIFY(!store.read(secretId).has_value());

        QVERIFY(transaction.commit());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({saved}))));
        transaction.finalize();
        QCOMPARE(store.read(secretId).value_or({}), QStringLiteral("plain-key"));
        QCOMPARE(store.read(secretId2).value_or({}), QStringLiteral("second-key"));
        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        QVariantList hydrated = service.pluginEntries().first().config
                                    .value(QStringLiteral("vendors")).toList();
        QCOMPARE(hydrated.first().toMap().value(QStringLiteral("apiKey")).toString(),
                 QStringLiteral("plain-key"));
        QCOMPARE(hydrated.at(1).toMap().value(QStringLiteral("apiKey")).toString(),
                 QStringLiteral("second-key"));

        // 重排使用稳定 itemKey，而不是数组下标；两份凭据不得串位。
        std::swap(hydrated[0], hydrated[1]);
        PluginConfigEntry reordered = service.pluginEntries().first();
        reordered.config.insert(QStringLiteral("vendors"), hydrated);
        SettingsPluginModel reorderedModel;
        reorderedModel.reload({reordered}, QJsonArray{saved}, dir.path(), service.activePath());
        QJsonArray reorderedExport;
        auto reorderTransaction = store.beginTransaction();
        QVERIFY2(reorderedModel.toPluginEntriesJson(
                     &reorderedExport, &error, &reorderTransaction), qPrintable(error));
        const QJsonObject reorderedSaved = reorderedExport.first().toObject();
        const QJsonArray reorderedVendors = reorderedSaved.value(QStringLiteral("config"))
                                                .toObject().value(QStringLiteral("vendors")).toArray();
        QCOMPARE(reorderedVendors.first().toObject()
                     .value(QStringLiteral("secretId")).toString(), token2);
        QCOMPARE(SecretStore::referenceId(reorderedVendors.first().toObject()
                                              .value(QStringLiteral("apiKey"))), secretId2);
        QVERIFY(reorderTransaction.commit());

        QJsonArray tamperedVendors = reorderedVendors;
        QJsonObject tamperedFirst = tamperedVendors.at(0).toObject();
        QJsonObject tamperedSecond = tamperedVendors.at(1).toObject();
        const QJsonValue firstReference = tamperedFirst.value(QStringLiteral("apiKey"));
        tamperedFirst[QStringLiteral("apiKey")] = tamperedSecond.value(QStringLiteral("apiKey"));
        tamperedSecond[QStringLiteral("apiKey")] = firstReference;
        tamperedVendors[0] = tamperedFirst;
        tamperedVendors[1] = tamperedSecond;
        QJsonObject tamperedSaved = reorderedSaved;
        QJsonObject tamperedConfig = tamperedSaved.value(QStringLiteral("config")).toObject();
        tamperedConfig[QStringLiteral("vendors")] = tamperedVendors;
        tamperedSaved[QStringLiteral("config")] = tamperedConfig;
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({tamperedSaved}))));
        ConfigService tamperedService;
        QVERIFY(tamperedService.load(dir.path(), dir.path()));
        const QVariantList tamperedHydrated = tamperedService.pluginEntries().first().config
                                                  .value(QStringLiteral("vendors")).toList();
        QVERIFY(!tamperedHydrated.at(0).toMap().contains(QStringLiteral("apiKey")));
        QVERIFY(!tamperedHydrated.at(1).toMap().contains(QStringLiteral("apiKey")));

        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({reorderedSaved}))));
        reorderTransaction.finalize();

        ConfigService reorderedService;
        QVERIFY(reorderedService.load(dir.path(), dir.path()));
        const QVariantList reorderedHydrated = reorderedService.pluginEntries().first().config
                                                   .value(QStringLiteral("vendors")).toList();
        QCOMPARE(reorderedHydrated.first().toMap()
                     .value(QStringLiteral("apiKey")).toString(), QStringLiteral("second-key"));
        QCOMPARE(reorderedHydrated.at(1).toMap()
                     .value(QStringLiteral("apiKey")).toString(), QStringLiteral("plain-key"));

        // 删除第二项时，配置与密钥库在同一事务中清掉对应孤儿密钥。
        PluginConfigEntry shortened = reorderedService.pluginEntries().first();
        shortened.config.insert(QStringLiteral("vendors"),
                                QVariantList{reorderedHydrated.first()});
        SettingsPluginModel shortenedModel;
        shortenedModel.reload({shortened}, QJsonArray{reorderedSaved}, dir.path(),
                              reorderedService.activePath());
        QJsonArray shortenedExport;
        auto deleteTransaction = store.beginTransaction();
        QVERIFY2(shortenedModel.toPluginEntriesJson(
                     &shortenedExport, &error, &deleteTransaction), qPrintable(error));
        QVERIFY(deleteTransaction.commit());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins(
                                            {shortenedExport.first().toObject()}))));
        deleteTransaction.finalize();
        QCOMPARE(store.read(secretId2).value_or({}), QStringLiteral("second-key"));
        QVERIFY(!store.read(secretId).has_value());
    }

    void legacyDottedNestedSecretMigratesToJsonPointer() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString token = QStringLiteral("stable-account");
        const QJsonArray settings{QJsonObject{
            {QStringLiteral("group"), QStringLiteral("账户")},
            {QStringLiteral("fields"), QJsonArray{QJsonObject{
                {QStringLiteral("key"), QStringLiteral("accounts")},
                {QStringLiteral("type"), QStringLiteral("array")},
                {QStringLiteral("itemKey"), QStringLiteral("uid")},
                {QStringLiteral("items"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("fields"), QJsonArray{QJsonObject{
                        {QStringLiteral("key"), QStringLiteral("token")},
                        {QStringLiteral("type"), QStringLiteral("string")},
                        {QStringLiteral("secret"), true}
                    }}}
                }}
            }}}
        }};
        const QString pluginDir = dir.filePath(QStringLiteral("plugins_py/nested_demo"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile sidecar(QDir(pluginDir).filePath(QStringLiteral("plugin.json")));
        QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(sidecar.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("py_nested_demo")},
            {QStringLiteral("settings"), settings}
        }).toJson(QJsonDocument::Compact)) > 0);
        sidecar.close();

        SecretStore store(dir.path());
        const QString legacyId = SecretStore::idForPluginField(
            QStringLiteral("py_nested_demo"),
            QStringLiteral("accounts.%1.token").arg(token));
        const QString pointerId = SecretStore::idForPluginPointer(
            QStringLiteral("py_nested_demo"),
            QStringLiteral("/accounts/%1/token").arg(token));
        QVERIFY(store.write(legacyId, QStringLiteral("legacy-value")));
        const QJsonObject rawEntry = plugin(QStringLiteral("py_nested_demo"), {
            {QStringLiteral("accounts"), QJsonArray{QJsonObject{
                {QStringLiteral("uid"), token},
                {QStringLiteral("token"), SecretStore::referenceValue(legacyId)}
            }}}
        });
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({rawEntry}))));

        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        const QVariantMap hydrated = service.pluginEntries().first().config
                                         .value(QStringLiteral("accounts"))
                                         .toList().first().toMap();
        QCOMPARE(hydrated.value(QStringLiteral("token")).toString(),
                 QStringLiteral("legacy-value"));

        SettingsPluginModel model;
        model.reload(service.pluginEntries(), QJsonArray{rawEntry}, dir.path(),
                     service.activePath());
        QJsonArray exported;
        QString error;
        auto transaction = store.beginTransaction();
        QVERIFY2(model.toPluginEntriesJson(&exported, &error, &transaction), qPrintable(error));
        const QJsonObject savedAccount = exported.first().toObject()
                                             .value(QStringLiteral("config")).toObject()
                                             .value(QStringLiteral("accounts")).toArray()
                                             .first().toObject();
        QCOMPARE(SecretStore::referenceId(savedAccount.value(QStringLiteral("token"))), pointerId);
        QVERIFY(transaction.commit());
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins(
                                            {exported.first().toObject()}))));
        transaction.finalize();
        QVERIFY(!store.read(legacyId).has_value());
        QCOMPARE(store.read(pointerId).value_or({}), QStringLiteral("legacy-value"));
    }

    void pluginMoveKeepsRegionsSeparateAndNormalizesOrder() {
        auto entry = [](const QString& id, const QString& position, int order) {
            PluginConfigEntry value;
            value.id = id;
            value.position = position;
            value.order = order;
            return value;
        };

        SettingsPluginModel model;
        model.reload({entry(QStringLiteral("left-first"), QStringLiteral("left"), 12),
                      entry(QStringLiteral("left-second"), QStringLiteral("left"), 36),
                      entry(QStringLiteral("right-only"), QStringLiteral("right"), 42)},
                     {}, {}, {});

        SettingsPluginSortModel proxy;
        proxy.setSourceModel(&model);
        proxy.sort(0);
        QCOMPARE(proxy.rowForId(QStringLiteral("left-first")), 0);
        QCOMPARE(proxy.rowForId(QStringLiteral("left-second")), 1);
        QCOMPARE(proxy.rowForId(QStringLiteral("right-only")), 2);
        QVERIFY(!proxy.canMoveUp(0));                   // 左侧第一项不能跨区域上移
        QVERIFY(proxy.canMoveUp(1));
        QVERIFY(proxy.moveUp(1));
        QCOMPARE(proxy.rowForId(QStringLiteral("left-second")), 0);
        QCOMPARE(proxy.rowForId(QStringLiteral("left-first")), 1);
        QVERIFY(!proxy.canMoveUp(0));                   // 移到首位后不能再上移
        QVERIFY(!proxy.canMoveUp(2));                   // 右侧只有一项，不能移动

        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue& value : exported) {
            const QJsonObject object = value.toObject();
            byId.insert(object.value(QStringLiteral("id")).toString(), object);
        }
        QCOMPARE(byId.value(QStringLiteral("left-second")).value(QStringLiteral("order")).toInt(), 0);
        QCOMPARE(byId.value(QStringLiteral("left-first")).value(QStringLiteral("order")).toInt(), 1);
        QCOMPARE(byId.value(QStringLiteral("right-only")).value(QStringLiteral("order")).toInt(), 42);

        // 禁用后的左侧插件应整体落在所有启用插件之后；组合分组键还要让 QML
        // 区分“已启用 · 左侧区域”和“未启用 · 左侧区域”。
        QVERIFY(model.setData(model.index(0), false, SettingsPluginModel::EnabledRole));
        QCOMPARE(proxy.rowForId(QStringLiteral("left-second")), 0);
        QCOMPARE(proxy.rowForId(QStringLiteral("right-only")), 1);
        QCOMPARE(proxy.rowForId(QStringLiteral("left-first")), 2);
        QCOMPARE(proxy.roleValue(0, QStringLiteral("groupKey")).toString(),
                 QStringLiteral("enabled-left"));
        QCOMPARE(proxy.roleValue(2, QStringLiteral("groupKey")).toString(),
                 QStringLiteral("disabled-left"));
    }

    void pluginDraftDirtyOnlyAfterActualEdit() {
        PluginConfigEntry entry;
        entry.id = QStringLiteral("py_countdown");
        entry.position = QStringLiteral("right");
        entry.config = {{QStringLiteral("mode"), QStringLiteral("每日重复")}};

        SettingsPluginModel model;
        model.reload({entry}, QJsonArray{plugin(QStringLiteral("py_countdown"),
                                                {{QStringLiteral("mode"),
                                                  QStringLiteral("每日重复")}})},
                     {}, {});
        // SettingsPluginModel 不再按具体插件 ID 隐式迁移，原值保持且不标脏。
        QVERIFY(!model.isDirty());
        QVERIFY(model.setConfigValue(QStringLiteral("py_countdown"),
                                     QStringLiteral("mode"), QStringLiteral("每日重复")));
        QVERIFY(!model.isDirty());

        QVERIFY(model.setConfigValue(QStringLiteral("py_countdown"),
                                     QStringLiteral("mode"), QStringLiteral("daily")));
        QVERIFY(model.isDirty());
    }
};

QTEST_GUILESS_MAIN(ConfigServiceTest)
#include "ConfigServiceTest.moc"
