// PluginSlotHealthTest.cpp — 验证槽位上下文注入、弹窗服务与稳定启动耗时
#include "PopupService.h"
#include <QQuickWindow>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryFile>
#include <QtTest>

#include <memory>

class UiMetricsStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int radiusS READ radiusS CONSTANT)
public:
    using QObject::QObject;
    int radiusS() const { return 8; }
};

class ThemeStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap appearance READ appearance CONSTANT)
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
public:
    using QObject::QObject;
    QVariantMap appearance() const {
        return {
            {QStringLiteral("items"), QVariantMap{
                {QStringLiteral("decoration"), QStringLiteral("none")},
                {QStringLiteral("paddingX"), 0},
                {QStringLiteral("paddingY"), 0}}},
            {QStringLiteral("motion"), QVariantMap{{QStringLiteral("duration"), 0}}},
        };
    }
    QVariantMap colors() const {
        return {
            {QStringLiteral("panel"), QStringLiteral("#313244")},
            {QStringLiteral("hover"), QStringLiteral("#45475a")},
            {QStringLiteral("border"), QStringLiteral("#45475a")},
            {QStringLiteral("danger"), QStringLiteral("#f38ba8")},
        };
    }
};

class WorkerHealthStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap health READ health NOTIFY healthChanged)
public:
    using QObject::QObject;
    QVariantMap health() const {
        return {
            {QStringLiteral("state"), QStringLiteral("running")},
            {QStringLiteral("loadDurationMs"), m_loadDurationMs},
            {QStringLiteral("queueDepth"), 0},
        };
    }
    void pulse() { Q_EMIT healthChanged(); }
    void setLoadDurationMs(qint64 value) {
        m_loadDurationMs = value;
        Q_EMIT healthChanged();
    }
Q_SIGNALS:
    void healthChanged();
private:
    qint64 m_loadDurationMs = 0;
};

class PluginContextStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap screenGeometry READ screenGeometry WRITE setScreenGeometry)
    Q_PROPERTY(QString barEdge READ barEdge WRITE setBarEdge)
    Q_PROPERTY(QObject* popupService MEMBER m_popupService)
    Q_PROPERTY(QObject* actions MEMBER m_actions)
public:
    using QObject::QObject;
    QVariantMap screenGeometry() const { return m_screenGeometry; }
    void setScreenGeometry(const QVariantMap& value) { m_screenGeometry = value; }
    QString barEdge() const { return m_barEdge; }
    void setBarEdge(const QString& value) { m_barEdge = value; }
    Q_INVOKABLE void notifyBarPressed() { Q_EMIT barPressed(); }
Q_SIGNALS:
    void barPressed();
private:
    QObject* m_popupService = nullptr;
    QObject* m_actions = nullptr;
    QVariantMap m_screenGeometry;
    QString m_barEdge = QStringLiteral("top");
};

class BarWindowStub final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
Q_SIGNALS:
    void barPressed();
};

class BarConfigStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString barEdge READ barEdge CONSTANT)
public:
    using QObject::QObject;
    QString barEdge() const { return QStringLiteral("top"); }
};

class PluginModelStub final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    QVariantMap latestHealth() const { return m_latestHealth; }
    int reportCount() const { return m_reportCount; }

    Q_INVOKABLE void reportHealth(const QString&, const QVariantMap& health) {
        m_latestHealth = health;
        ++m_reportCount;
        Q_EMIT healthReported();
    }
    Q_INVOKABLE void reportContentWidth(const QString&, int) {}

Q_SIGNALS:
    void healthReported();

private:
    QVariantMap m_latestHealth;
    int m_reportCount = 0;
};

class PluginSlotHealthTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        qmlRegisterSingletonType<UiMetricsStub>(
            "XuanHub", 1, 0, "UiMetrics",
            [](QQmlEngine*, QJSEngine*) -> QObject* { return new UiMetricsStub; });
    }

    void popupUsesOnlyScopedServicesForPlacementAndDismissal() {
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        PopupService service;
        PluginContextStub context;
        context.setProperty("popupService", QVariant::fromValue<QObject*>(&service));
        context.setScreenGeometry({{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                                   {QStringLiteral("width"), 800}, {QStringLiteral("height"), 600}});
        QQuickWindow window;
        window.setGeometry(100, 100, 40, 32);
        QQuickItem anchor(window.contentItem());
        anchor.setSize(QSizeF(40, 32));
        window.show();
        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginPopup.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        const QVariantMap properties{
            {QStringLiteral("anchorItem"), QVariant::fromValue(&anchor)},
            {QStringLiteral("pluginContext"), QVariant::fromValue(&context)},
            {QStringLiteral("closeOnDeactivate"), false},
            {QStringLiteral("width"), 180}, {QStringLiteral("height"), 100},
        };
        std::unique_ptr<QObject> first(component.createWithInitialProperties(properties));
        std::unique_ptr<QObject> second(component.createWithInitialProperties(properties));
        QVERIFY2(first && second, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(first.get(), "openAnchored"));
        QTRY_COMPARE(first->property("opacity").toDouble(), 1.0);
        QVERIFY(first->property("visible").toBool());
        QCOMPARE(service.activePopup(), first.get());
        QVERIFY(first->property("x").toInt() >= 0);
        QVERIFY(first->property("y").toInt() + 100 <= 600);
        QVERIFY(QMetaObject::invokeMethod(second.get(), "openAnchored"));
        QTRY_VERIFY(!first->property("visible").toBool());
        QCOMPARE(service.activePopup(), second.get());
        context.notifyBarPressed();
        QTRY_VERIFY(!second->property("visible").toBool());
        QVERIFY(!service.activePopup());
        QVERIFY(warnings.isEmpty());
    }

    void initializesContextAndKeepsStartupDurationFixed() {
        QTemporaryFile pluginQml;
        QVERIFY(pluginQml.open());
        QVERIFY(pluginQml.write(
            "import QtQuick\n"
            "Item { objectName: \"loadedPlugin\"; property var plugin; property var pluginContext;\n"
            "property bool contextReady: false; width: 40; height: 40;\n"
            "onPluginChanged: contextReady = !!pluginContext }\n") > 0);
        pluginQml.flush();

        QQmlEngine engine;
        ThemeStub theme;
        WorkerHealthStub worker;
        PluginContextStub pluginContext;
        BarWindowStub barWindow;
        BarConfigStub barConfig;
        PluginModelStub pluginModel;
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("pluginModel"), &pluginModel);
        engine.rootContext()->setContextProperty(QStringLiteral("barWindow"), &barWindow);
        engine.rootContext()->setContextProperty(QStringLiteral("barConfig"), &barConfig);
        engine.rootContext()->setContextProperty(
            QStringLiteral("barScreenGeometry"),
            QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                        {QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}});

        const QString slotPath = QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginSlot.qml");
        QQmlComponent component(&engine, QUrl::fromLocalFile(slotPath));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        const QVariantMap model{
            {QStringLiteral("id"), QStringLiteral("py_test")},
            {QStringLiteral("viewModel"), QVariant::fromValue(&worker)},
            {QStringLiteral("pluginContext"), QVariant::fromValue(&pluginContext)},
            {QStringLiteral("qmlUrl"), QUrl::fromLocalFile(pluginQml.fileName())},
            {QStringLiteral("width"), 0},
            {QStringLiteral("effectiveWidth"), 0},
            {QStringLiteral("loadDurationMs"), 17},
            {QStringLiteral("lastError"), QString{}},
        };
        std::unique_ptr<QObject> root(component.createWithInitialProperties(
            {{QStringLiteral("model"), model}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        QVERIFY(qobject_cast<QQuickItem*>(root.get()));
        auto* loadedPlugin = root->findChild<QObject*>(QStringLiteral("loadedPlugin"));
        QVERIFY(loadedPlugin);
        // 声明但未赋值的 var 初始为 undefined，也必须在 ViewModel 之前注入。
        QCOMPARE(loadedPlugin->property("pluginContext").value<QObject*>(), &pluginContext);
        QCOMPARE(loadedPlugin->property("plugin").value<QObject*>(), &worker);
        QVERIFY(loadedPlugin->property("contextReady").toBool());

        QTRY_VERIFY_WITH_TIMEOUT(
            pluginModel.latestHealth().value(QStringLiteral("loadDurationMs")).toLongLong()
                >= 18,
            1000);
        const qint64 completedDuration = pluginModel.latestHealth()
                                             .value(QStringLiteral("loadDurationMs"))
                                             .toLongLong();
        const int reportsBeforePulse = pluginModel.reportCount();

        QTest::qWait(150);
        worker.pulse();
        QTRY_VERIFY_WITH_TIMEOUT(pluginModel.reportCount() > reportsBeforePulse, 1000);
        QCOMPARE(pluginModel.latestHealth()
                     .value(QStringLiteral("loadDurationMs")).toLongLong(),
                 completedDuration);

        worker.setLoadDurationMs(73);
        QTRY_COMPARE_WITH_TIMEOUT(
            pluginModel.latestHealth().value(QStringLiteral("loadDurationMs")).toLongLong(),
            completedDuration + 73, 1000);
        const qint64 completedWorkerDuration = pluginModel.latestHealth()
                                                   .value(QStringLiteral("loadDurationMs"))
                                                   .toLongLong();
        QTest::qWait(150);
        worker.pulse();
        QTRY_VERIFY_WITH_TIMEOUT(pluginModel.reportCount() > reportsBeforePulse + 2, 1000);
        QCOMPARE(pluginModel.latestHealth()
                     .value(QStringLiteral("loadDurationMs")).toLongLong(),
                 completedWorkerDuration);
    }
};

QTEST_MAIN(PluginSlotHealthTest)
#include "PluginSlotHealthTest.moc"
