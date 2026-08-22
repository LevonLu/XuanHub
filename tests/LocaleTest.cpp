// LocaleTest.cpp — JSON 语言包隔离、回退和系统语言解析回归测试
#include "JsonTranslator.h"
#include "LocaleManager.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class LocaleTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void requestedAndSystemLocalesAreNormalized() {
        QCOMPARE(LocaleManager::normalizeRequested(QStringLiteral("ZH-cn")), QStringLiteral("zh-CN"));
        QCOMPARE(LocaleManager::normalizeRequested(QStringLiteral("en-us")), QStringLiteral("en-US"));
        QCOMPARE(LocaleManager::normalizeRequested(QStringLiteral("fr-FR")), QStringLiteral("system"));
        QCOMPARE(LocaleManager::resolveActive(QStringLiteral("system"), QStringLiteral("zh-Hans-CN")),
                 QStringLiteral("zh-CN"));
        QCOMPARE(LocaleManager::resolveActive(QStringLiteral("system"), QStringLiteral("de-DE")),
                 QStringLiteral("en-US"));
        QCOMPARE(LocaleManager::resolveActive(QStringLiteral("en-US"), QStringLiteral("zh-CN")),
                 QStringLiteral("en-US"));
    }

    void pluginCatalogIsIsolatedAndMissingTextFallsBack() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pluginDir = dir.filePath(QStringLiteral("plugins/demo"));
        QVERIFY(QDir().mkpath(pluginDir + QStringLiteral("/i18n")));

        QFile metadata(pluginDir + QStringLiteral("/metadata.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(R"JSON({"id":"demo"})JSON");
        metadata.close();

        QFile catalog(pluginDir + QStringLiteral("/i18n/en-US.json"));
        QVERIFY(catalog.open(QIODevice::WriteOnly));
        catalog.write(R"JSON({
          "locale":"en-US",
          "contexts":{
            "plugin.demo.main":{"你好":"Hello"},
            "xuanhub.settings":{"设置":"Hijacked"},
            "plugin.other.main":{"越界":"Foreign"}
          }
        })JSON");
        catalog.close();

        JsonTranslator translator;
        QVERIFY(!translator.loadLocale(QStringLiteral("en-US"), dir.path()));
        QCOMPARE(translator.translate("plugin.demo.main", "你好"), QStringLiteral("Hello"));
        QVERIFY(translator.translate("plugin.demo.main", "缺项").isEmpty());
        QVERIFY(translator.translate("xuanhub.settings", "设置").isEmpty());
        QVERIFY(translator.translate("plugin.other.main", "越界").isEmpty());
    }

    void damagedCatalogDoesNotLeakPreviousLocale() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pluginDir = dir.filePath(QStringLiteral("plugins_py/demo"));
        QVERIFY(QDir().mkpath(pluginDir + QStringLiteral("/i18n")));
        QFile metadata(pluginDir + QStringLiteral("/plugin.json"));
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write(R"JSON({"id":"demo"})JSON");
        metadata.close();
        QFile catalog(pluginDir + QStringLiteral("/i18n/zh-CN.json"));
        QVERIFY(catalog.open(QIODevice::WriteOnly));
        catalog.write("{ damaged");
        catalog.close();

        JsonTranslator translator;
        QVERIFY(!translator.loadLocale(QStringLiteral("zh-CN"), dir.path()));
        QVERIFY(translator.translate("plugin.demo.main", "你好").isEmpty());
    }

    void localeManagerPublishesRetainedState() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        xuanhub::EventBus bus;
        LocaleManager manager(&bus, dir.path());
        manager.setRequestedLanguage(QStringLiteral("en-US"));

        const QVariantMap envelope = bus.state(QStringLiteral("app.locale"));
        QCOMPARE(envelope.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("state"));
        QCOMPARE(envelope.value(QStringLiteral("source")).toString(),
                 QStringLiteral("app"));
        const QVariantMap data = envelope.value(QStringLiteral("data")).toMap();
        QCOMPARE(data.value(QStringLiteral("locale")).toString(), QStringLiteral("en-US"));
        QCOMPARE(data.value(QStringLiteral("requestedLanguage")).toString(),
                 QStringLiteral("en-US"));
    }
};

QTEST_GUILESS_MAIN(LocaleTest)
#include "LocaleTest.moc"
