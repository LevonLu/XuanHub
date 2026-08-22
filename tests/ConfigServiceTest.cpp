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

namespace {
bool writeJson(const QString& directory, const QJsonDocument& doc) {
    QFile file(directory + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(doc.toJson(QJsonDocument::Compact)) >= 0;
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

    void schemaEnumFormatsAndLegacyBuiltInValuesAreMigrated() {
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

        PluginConfigEntry countdown;
        countdown.id = QStringLiteral("py_countdown");
        countdown.config = {{QStringLiteral("mode"), QStringLiteral("每日重复")}};
        PluginConfigEntry todo;
        todo.id = QStringLiteral("py_todo");
        todo.config = {{QStringLiteral("barMode"), QStringLiteral("最近一项")}};
        PluginConfigEntry enumEntry;
        enumEntry.id = QStringLiteral("py_enum");
        enumEntry.config = {{QStringLiteral("legacy"), QStringLiteral("one")},
                            {QStringLiteral("stable"), QStringLiteral("daily")}};

        SettingsPluginModel model;
        model.reload({countdown, todo, enumEntry}, {}, dir.path(),
                     dir.filePath(QStringLiteral("config.json")));
        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue& value : exported) {
            const QJsonObject object = value.toObject();
            byId.insert(object.value(QStringLiteral("id")).toString(), object);
        }
        QCOMPARE(byId.value(QStringLiteral("py_countdown")).value(QStringLiteral("config"))
                     .toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("daily"));
        QCOMPARE(byId.value(QStringLiteral("py_todo")).value(QStringLiteral("config"))
                     .toObject().value(QStringLiteral("barMode")).toString(), QStringLiteral("latest"));

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

    void usageHubVendorSecretMigratesAndHydrates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QJsonObject vendor{{QStringLiteral("id"), QStringLiteral("deepseek")},
                                 {QStringLiteral("name"), QStringLiteral("DeepSeek")},
                                 {QStringLiteral("apiKey"), QStringLiteral("plain-key")}};
        const QJsonObject rawEntry = plugin(QStringLiteral("py_usage_hub"),
                                            {{QStringLiteral("vendors"), QJsonArray{vendor}}});
        PluginConfigEntry entry;
        entry.id = QStringLiteral("py_usage_hub");
        entry.position = QStringLiteral("right");
        entry.config = rawEntry.value(QStringLiteral("config")).toObject().toVariantMap();

        SettingsPluginModel model;
        model.reload({entry}, QJsonArray{rawEntry}, {}, dir.filePath(QStringLiteral("config.json")));
        QJsonArray exported;
        QString error;
        QVERIFY2(model.toPluginEntriesJson(&exported, &error), qPrintable(error));

        const QJsonObject saved = exported.first().toObject();
        const QJsonObject savedVendor = saved.value(QStringLiteral("config")).toObject()
                                           .value(QStringLiteral("vendors")).toArray().first().toObject();
        const QString token = savedVendor.value(QStringLiteral("secretId")).toString();
        QVERIFY(!token.isEmpty());
        const QString secretId = SecretStore::idForPluginField(
            QStringLiteral("py_usage_hub"), QStringLiteral("vendors.%1.apiKey").arg(token));
        QCOMPARE(SecretStore::referenceId(savedVendor.value(QStringLiteral("apiKey"))), secretId);
        QCOMPARE(SecretStore(dir.path()).read(secretId).value_or({}), QStringLiteral("plain-key"));

        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({saved}))));
        ConfigService service;
        QVERIFY(service.load(dir.path(), dir.path()));
        const QVariantMap hydrated = service.pluginEntries().first().config
                                         .value(QStringLiteral("vendors")).toList().first().toMap();
        QCOMPARE(hydrated.value(QStringLiteral("apiKey")).toString(), QStringLiteral("plain-key"));
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
        // 兼容迁移只建立内存草稿，不应让一次纯语言保存变成插件配置修改。
        QVERIFY(!model.isDirty());
        QVERIFY(model.setConfigValue(QStringLiteral("py_countdown"),
                                     QStringLiteral("mode"), QStringLiteral("daily")));
        QVERIFY(!model.isDirty());

        QVERIFY(model.setConfigValue(QStringLiteral("py_countdown"),
                                     QStringLiteral("mode"), QStringLiteral("date")));
        QVERIFY(model.isDirty());
    }
};

QTEST_GUILESS_MAIN(ConfigServiceTest)
#include "ConfigServiceTest.moc"
