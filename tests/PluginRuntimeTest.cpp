// PluginRuntimeTest.cpp — V1 兼容适配与 V2 统一运行时回归测试
#include "PluginRuntime.h"

#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPlugin2.h"

#include <QtTest>

class FakeV1Plugin final : public QObject, public xuanhub::IBarPlugin {
    Q_OBJECT
    Q_INTERFACES(xuanhub::IBarPlugin)
public:
    xuanhub::PluginMetadata metadata() const override {
        return {QStringLiteral("v1"), QStringLiteral("V1"),
                QStringLiteral("1.0.0"), QStringLiteral("Test")};
    }
    bool init(xuanhub::BarPluginContext*) override { initialized = true; return true; }
    QObject* viewModel() override { return this; }
    QUrl qmlUrl() const override { return QUrl(QStringLiteral("V1.qml")); }
    void shutdown() override { stopped = true; }
    bool initialized = false;
    bool stopped = false;
};

class FakeV2Plugin final : public QObject, public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_INTERFACES(xuanhub::IBarPlugin2)
public:
    xuanhub::PluginDescriptor descriptor() const override {
        xuanhub::PluginDescriptor value;
        value.id = QStringLiteral("v2");
        value.displayName = QStringLiteral("V2");
        value.version = QStringLiteral("2.0.0");
        value.apiVersion = 2;
        return value;
    }
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override {
        initialized = true;
        dependencyAvailable = context.hasDependency(QStringLiteral("base"));
        receivedHostServices = context.hostServices;
        return xuanhub::PluginInitResult::succeeded();
    }
    xuanhub::PluginConfigValidationResult validateConfig(
        const QVariantMap& config) const override {
        return config.contains(QStringLiteral("reject"))
            ? xuanhub::PluginConfigValidationResult::rejected(QStringLiteral("rejected"))
            : xuanhub::PluginConfigValidationResult::accepted();
    }
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap&) override {
        return xuanhub::PluginConfigApplyResult::applied();
    }
    QObject* viewModel() override { return this; }
    QUrl qmlUrl() const override { return QUrl(QStringLiteral("V2.qml")); }
    void shutdown() override { stopped = true; }
    bool initialized = false;
    bool stopped = false;
    bool dependencyAvailable = false;
    xuanhub::IHostServices* receivedHostServices = nullptr;
};

class PluginRuntimeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void adaptsV1WithoutChangingItsInterface() {
        FakeV1Plugin plugin;
        QString error;
        auto runtime = PluginRuntime::create(
            &plugin, QStringLiteral("xuanhub.BarPlugin/1.0"), &error);
        QVERIFY2(runtime, qPrintable(error));
        QVERIFY(!runtime->isV2());
        QCOMPARE(runtime->descriptor().id, QStringLiteral("v1"));
        QVERIFY(runtime->initialize(nullptr, &error));
        QCOMPARE(runtime->viewModel(), &plugin);
        runtime->shutdown();
        QVERIFY(plugin.initialized);
        QVERIFY(plugin.stopped);
    }

    void drivesV2StructuredLifecycle() {
        FakeV2Plugin plugin;
        QString error;
        auto runtime = PluginRuntime::create(
            &plugin, QStringLiteral("xuanhub.BarPlugin/2.0"), &error);
        QVERIFY2(runtime, qPrintable(error));
        QVERIFY(runtime->isV2());
        QVERIFY(runtime->descriptorMatches(plugin.descriptor(), &error));
        xuanhub::BarPluginContext context;
        context.dependencyVersions.insert(QStringLiteral("base"), QStringLiteral("1.0.0"));
        context.hostServices = reinterpret_cast<xuanhub::IHostServices*>(quintptr(1));
        QVERIFY(runtime->initialize(&context, &error));
        QVERIFY(plugin.dependencyAvailable);
        QCOMPARE(plugin.receivedHostServices, context.hostServices);
        QCOMPARE(runtime->applyConfig({}).status,
                 PluginRuntime::ApplyStatus::Applied);
        QCOMPARE(runtime->applyConfig({{QStringLiteral("reject"), true}}).status,
                 PluginRuntime::ApplyStatus::Rejected);
        runtime->shutdown();
        QVERIFY(plugin.initialized);
        QVERIFY(plugin.stopped);
    }
};

QTEST_GUILESS_MAIN(PluginRuntimeTest)
#include "PluginRuntimeTest.moc"
