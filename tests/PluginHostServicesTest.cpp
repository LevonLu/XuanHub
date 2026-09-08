// PluginHostServicesTest.cpp — 验证宿主服务权限、身份与私有命名空间隔离
#include <QtTest>

#include "PluginHostServices.h"
#include "PluginContextObject.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>

class PluginHostServicesTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void isolatesStorageAndSecrets();
    void rejectsPathEscapesAndJunctions();
    void enforcesPermissionAndBindsIdentity();
    void exposesQmlContextWithoutIdentityOverride();
};

namespace {
PluginHostServices makeServices(const QString& configDir, const QString& id,
                                const QStringList& permissions,
                                xuanhub::EventBus* bus = nullptr) {
    PluginHostServices::Environment environment;
    environment.configDir = configDir;
    environment.eventBus = bus;
    environment.activeLocale = [] { return QStringLiteral("zh-CN"); };
    environment.currentPalette = [] {
        return QVariantMap{{QStringLiteral("bg"), QStringLiteral("#000000")}};
    };
    return PluginHostServices(id, xuanhub::HostPluginTrustLevel::TrustedInProcess,
                              permissions, permissions, std::move(environment));
}
} // namespace

void PluginHostServicesTest::isolatesStorageAndSecrets() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto first = makeServices(directory.path(), QStringLiteral("plugin.first"),
                              {QStringLiteral("storage.private"),
                               QStringLiteral("secrets.own")});
    auto second = makeServices(directory.path(), QStringLiteral("plugin.second"),
                               {QStringLiteral("storage.private"),
                                QStringLiteral("secrets.own")});

    QVERIFY(first.storage()->writeBytes(QStringLiteral("state/value.bin"), "first").success);
    QVERIFY(second.storage()->writeBytes(QStringLiteral("state/value.bin"), "second").success);
    QCOMPARE(first.storage()->readBytes(QStringLiteral("state/value.bin")).value.toByteArray(),
             QByteArray("first"));
    QCOMPARE(second.storage()->readBytes(QStringLiteral("state/value.bin")).value.toByteArray(),
             QByteArray("second"));

    QVERIFY(first.secrets()->writeSecret(QStringLiteral("token"), QStringLiteral("alpha")).success);
    QVERIFY(second.secrets()->writeSecret(QStringLiteral("token"), QStringLiteral("beta")).success);
    QCOMPARE(first.secrets()->readSecret(QStringLiteral("token")).value.toString(),
             QStringLiteral("alpha"));
    QCOMPARE(second.secrets()->readSecret(QStringLiteral("token")).value.toString(),
             QStringLiteral("beta"));
}

void PluginHostServicesTest::rejectsPathEscapesAndJunctions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto services = makeServices(directory.path(), QStringLiteral("plugin.safe"),
                                 {QStringLiteral("storage.private")});
    QVERIFY(!services.storage()->writeBytes(QStringLiteral("../outside.txt"), "bad").success);
    QVERIFY(!services.storage()->writeBytes(QStringLiteral("C:/outside.txt"), "bad").success);
    QVERIFY(!services.storage()->writeBytes(QStringLiteral("CON.txt"), "bad").success);

    const QString root = QDir(directory.path()).filePath(
        QStringLiteral("plugin-data/plugin.safe"));
    const QString outside = QDir(directory.path()).filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(outside));
    const QString junction = QDir(root).filePath(QStringLiteral("linked"));
    const int exitCode = QProcess::execute(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
         QDir::toNativeSeparators(junction), QDir::toNativeSeparators(outside)});
    if (exitCode != 0) QSKIP("当前环境不允许创建目录联接");
    const auto result = services.storage()->writeBytes(QStringLiteral("linked/escape.txt"), "bad");
    QVERIFY(!result.success);
    QCOMPARE(result.errorCode, QStringLiteral("invalid_path"));
}

void PluginHostServicesTest::enforcesPermissionAndBindsIdentity() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    xuanhub::EventBus bus;
    auto denied = makeServices(directory.path(), QStringLiteral("plugin.denied"), {}, &bus);
    QCOMPARE(denied.apiVersion(), 1);
    QCOMPARE(denied.pluginId(), QStringLiteral("plugin.denied"));
    QVERIFY(!denied.notifications()->show(QStringLiteral("title"),
                                           QStringLiteral("message")).success);
    QVERIFY(!denied.storage()->writeBytes(QStringLiteral("state.bin"), "bad").success);
    QVERIFY(!denied.secrets()->writeSecret(QStringLiteral("token"),
                                           QStringLiteral("bad")).success);
    QCOMPARE(denied.locale()->activeLocale(), QStringLiteral("zh-CN"));
    QCOMPARE(denied.theme()->currentPalette().value(QStringLiteral("bg")).toString(),
             QStringLiteral("#000000"));

    QSignalSpy spy(&bus, &xuanhub::EventBus::dispatched);
    auto allowed = makeServices(directory.path(), QStringLiteral("plugin.allowed"),
                                {QStringLiteral("notifications")}, &bus);
    QVERIFY(allowed.notifications()->show(QStringLiteral("title"),
                                          QStringLiteral("message")).success);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("app.notify"));
    QCOMPARE(spy.at(0).at(1).toMap().value(QStringLiteral("source")).toString(),
             QStringLiteral("plugin.allowed"));
}

void PluginHostServicesTest::exposesQmlContextWithoutIdentityOverride() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto services = makeServices(directory.path(), QStringLiteral("plugin.qml"),
                                 {QStringLiteral("storage.private")});
    PluginContextObject context(&services, nullptr, nullptr);
    QCOMPARE(context.contextVersion(), 1);
    auto service = std::make_unique<QObject>();
    context.setPopupService(service.get());
    context.setActions(service.get());
    QCOMPARE(context.popupService(), service.get());
    service.reset();
    QVERIFY(!context.popupService());
    QVERIFY(!context.actions());
    QCOMPARE(context.pluginId(), QStringLiteral("plugin.qml"));
    QCOMPARE(context.trustLevel(), QStringLiteral("trusted-in-process"));
    QCOMPARE(context.activeLocale(), QStringLiteral("zh-CN"));
    QVERIFY(context.hasPermission(QStringLiteral("storage.private")));
    QVERIFY(!context.hasPermission(QStringLiteral("notifications")));

    QSignalSpy geometrySpy(&context, &PluginContextObject::screenGeometryChanged);
    context.setScreenGeometry({{QStringLiteral("x"), 10},
                               {QStringLiteral("width"), 1920}});
    QCOMPARE(geometrySpy.size(), 1);
    QCOMPARE(context.screenGeometry().value(QStringLiteral("x")).toInt(), 10);
    QVERIFY(context.writeText(QStringLiteral("qml/state.txt"),
                              QStringLiteral("状态")).value(QStringLiteral("success")).toBool());
    QCOMPARE(context.readText(QStringLiteral("qml/state.txt")).value(
                 QStringLiteral("value")).toString(), QStringLiteral("状态"));
}

QTEST_APPLESS_MAIN(PluginHostServicesTest)
#include "PluginHostServicesTest.moc"
