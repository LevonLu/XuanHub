// ConfigServiceTest.cpp — 配置根类型、重复插件与 DPAPI 密钥引用回归测试
#include "ConfigService.h"
#include "SecretStore.h"
#include "SettingsPluginModel.h"
#include "SettingsPluginSortModel.h"

#include <QFile>
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

    void secretReferenceHydratesOnlyForOwningPlugin() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString secretId = SecretStore::idForPluginField(QStringLiteral("py_pet"),
                                                               QStringLiteral("aiApiKey"));
        SecretStore secrets(dir.path());
        QVERIFY(secrets.write(secretId, QStringLiteral("test-secret")));

        const QJsonObject secretConfig{{QStringLiteral("aiApiKey"),
                                        SecretStore::referenceValue(secretId)}};
        QVERIFY(writeJson(dir.path(), QJsonDocument(configWithPlugins({
            plugin(QStringLiteral("py_pet"), secretConfig)}))));

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
};

QTEST_GUILESS_MAIN(ConfigServiceTest)
#include "ConfigServiceTest.moc"
