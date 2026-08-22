// EventBusTest.cpp — 插件订阅作用域与卸载清理回归测试
#include "barplugin/EventBus.h"
#include "barplugin/PluginEventScope.h"

#include <QtTest>

class EventReceiver final : public QObject {
    Q_OBJECT
public slots:
    void onPayload(QVariantMap payload) {
        ++calls;
        lastValue = payload.value(QStringLiteral("value")).toInt();
    }
public:
    int calls = 0;
    int lastValue = 0;
};

class EventBusTest final : public QObject {
    Q_OBJECT

private slots:
    void scopeClearRemovesLiveReceiver() {
        xuanhub::EventBus bus;
        EventReceiver receiver;
        xuanhub::PluginEventScope scope(&bus);
        QVERIFY(scope.subscribe(QStringLiteral("plugin.test"), &receiver,
                                "onPayload(QVariantMap)"));

        bus.publish(QStringLiteral("plugin.test"), {{QStringLiteral("value"), 7}});
        QCOMPARE(receiver.calls, 1);
        QCOMPARE(receiver.lastValue, 7);

        scope.clear();
        QVERIFY(!scope.active());
        QVERIFY(!scope.subscribe(QStringLiteral("plugin.test"), &receiver,
                                 "onPayload(QVariantMap)"));
        bus.publish(QStringLiteral("plugin.test"), {{QStringLiteral("value"), 9}});
        QCOMPARE(receiver.calls, 1);
    }

    void scopeDestructionRemovesLeakedReceiver() {
        xuanhub::EventBus bus;
        EventReceiver receiver;
        {
            xuanhub::PluginEventScope scope(&bus);
            QVERIFY(scope.subscribe(QStringLiteral("plugin.leaked"), &receiver,
                                    "onPayload(QVariantMap)"));
        }

        // receiver 仍存活，模拟 DLL 忘记销毁订阅 QObject；作用域析构后不得再调用。
        bus.publish(QStringLiteral("plugin.leaked"));
        QCOMPARE(receiver.calls, 0);
    }

    void localeAndThemeEventsSkipClearedPluginScope() {
        xuanhub::EventBus bus;
        EventReceiver receiver;
        xuanhub::PluginEventScope scope(&bus);
        QVERIFY(scope.subscribe(QStringLiteral("app.locale.changed"), &receiver,
                                "onPayload(QVariantMap)"));
        QVERIFY(scope.subscribe(QStringLiteral("theme.changed"), &receiver,
                                "onPayload(QVariantMap)"));
        bus.publish(QStringLiteral("app.locale.changed"));
        bus.publish(QStringLiteral("theme.changed"));
        QCOMPARE(receiver.calls, 2);

        scope.clear();
        bus.publish(QStringLiteral("app.locale.changed"));
        bus.publish(QStringLiteral("theme.changed"));
        QCOMPARE(receiver.calls, 2);
    }
};

QTEST_GUILESS_MAIN(EventBusTest)
#include "EventBusTest.moc"
