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
    void onEnvelope(QVariantMap payload) {
        ++calls;
        lastEnvelope = std::move(payload);
    }
    void onResult(QVariantMap payload) {
        ++resultCalls;
        lastResult = std::move(payload);
    }
public:
    int calls = 0;
    int lastValue = 0;
    int resultCalls = 0;
    QVariantMap lastEnvelope;
    QVariantMap lastResult;
};

class RequestResponder final : public QObject {
    Q_OBJECT
public:
    explicit RequestResponder(xuanhub::PluginEventScope* scope) : scope(scope) {}

public slots:
    void onRequest(QVariantMap envelope) {
        ++calls;
        const QString requestId = envelope.value(QStringLiteral("requestId")).toString();
        firstResponseAccepted = scope && scope->respond(
            requestId, {{QStringLiteral("answer"), 42}});
        secondResponseAccepted = scope && scope->respond(
            requestId, {{QStringLiteral("answer"), 99}});
    }

public:
    xuanhub::PluginEventScope* scope = nullptr;
    int calls = 0;
    bool firstResponseAccepted = false;
    bool secondResponseAccepted = false;
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

    void v2DeliveryIsQueuedAndCarriesBoundEnvelope() {
        xuanhub::EventBus bus;
        EventReceiver receiver;
        xuanhub::PluginEventScope scope(&bus, QStringLiteral("demo"));
        QVERIFY(scope.subscribeV2(QStringLiteral("plugin.demo.tick"), &receiver,
                                  "onEnvelope(QVariantMap)"));

        QVERIFY(scope.publish(QStringLiteral("plugin.demo.tick"),
                              {{QStringLiteral("value"), 11}}));
        QCOMPARE(receiver.calls, 0); // V2 明确按事件循环排队，不同步回调插件。
        QTRY_COMPARE(receiver.calls, 1);
        QCOMPARE(receiver.lastEnvelope.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(receiver.lastEnvelope.value(QStringLiteral("source")).toString(),
                 QStringLiteral("plugin.demo"));
        QCOMPARE(receiver.lastEnvelope.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("event"));
        QCOMPARE(receiver.lastEnvelope.value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("value")).toInt(), 11);
        QVERIFY(!receiver.lastEnvelope.value(QStringLiteral("timestamp")).toString().isEmpty());

        QVERIFY(!scope.publish(QStringLiteral("plugin.other.tick")));
        QVERIFY(!scope.publish(QStringLiteral("not a topic")));
        QVERIFY(!scope.publish(QStringLiteral("plugin.demo.unsafe"),
                               {{QStringLiteral("bytes"), QByteArray("not-json")}}));
    }

    void crossNamespaceSubscriptionRequiresPermission() {
        xuanhub::EventBus bus;
        xuanhub::PluginEventScope publisher(&bus, QStringLiteral("producer"));
        xuanhub::PluginEventScope denied(&bus, QStringLiteral("denied"));
        xuanhub::PluginEventScope allowed(
            &bus, QStringLiteral("allowed"),
            {QStringLiteral("events.subscribe:plugin.producer.*")});
        EventReceiver deniedReceiver;
        EventReceiver allowedReceiver;

        QVERIFY(!denied.subscribeV2(QStringLiteral("plugin.producer.changed"),
                                    &deniedReceiver, "onEnvelope(QVariantMap)"));
        QVERIFY(allowed.subscribeV2(QStringLiteral("plugin.producer.changed"),
                                    &allowedReceiver, "onEnvelope(QVariantMap)"));
        QVERIFY(publisher.publish(QStringLiteral("plugin.producer.changed")));
        QTRY_COMPARE(allowedReceiver.calls, 1);
        QCOMPARE(deniedReceiver.calls, 0);
    }

    void retainedStateReplaysAndFollowsOwnerPolicy() {
        xuanhub::EventBus bus;
        xuanhub::PluginEventScope owner(&bus, QStringLiteral("owner"));
        QVERIFY(owner.setState(QStringLiteral("plugin.owner.status"),
                               {{QStringLiteral("ready"), true}}));

        xuanhub::PluginEventScope observer(
            &bus, QStringLiteral("observer"),
            {QStringLiteral("events.subscribe:plugin.owner.*")});
        EventReceiver receiver;
        xuanhub::EventSubscriptionOptions replay;
        replay.replayRetained = true;
        QVERIFY(observer.subscribeV2(QStringLiteral("plugin.owner.status"), &receiver,
                                     "onEnvelope(QVariantMap)", replay));
        QTRY_COMPARE(receiver.calls, 1);
        QCOMPARE(receiver.lastEnvelope.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("state"));
        QVERIFY(receiver.lastEnvelope.value(QStringLiteral("data")).toMap()
                    .value(QStringLiteral("ready")).toBool());

        owner.clear();
        QVERIFY(bus.state(QStringLiteral("plugin.owner.status")).isEmpty());

        QVERIFY(bus.setState(QStringLiteral("app.locale"),
                             {{QStringLiteral("locale"), QStringLiteral("en-US")}}));
        EventReceiver lateReceiver;
        QVERIFY(observer.subscribeV2(QStringLiteral("app.locale"), &lateReceiver,
                                     "onEnvelope(QVariantMap)", replay));
        QTRY_COMPARE(lateReceiver.calls, 1);
        QCOMPARE(lateReceiver.lastEnvelope.value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("locale")).toString(), QStringLiteral("en-US"));
    }

    void requestResponseAcceptsOnlyFirstResponse() {
        xuanhub::EventBus bus;
        xuanhub::PluginEventScope requester(&bus, QStringLiteral("requester"));
        xuanhub::PluginEventScope responderScope(
            &bus, QStringLiteral("responder"),
            {QStringLiteral("events.subscribe:plugin.requester.*")});
        RequestResponder responder(&responderScope);
        EventReceiver result;
        QVERIFY(responderScope.subscribeV2(QStringLiteral("plugin.requester.query"),
                                           &responder, "onRequest(QVariantMap)"));

        const QString requestId = requester.request(
            QStringLiteral("plugin.requester.query"),
            {{QStringLiteral("question"), QStringLiteral("meaning")}},
            &result, "onResult(QVariantMap)", 500);
        QVERIFY(!requestId.isEmpty());
        QTRY_COMPARE(result.resultCalls, 1);
        QVERIFY(result.lastResult.value(QStringLiteral("success")).toBool());
        QCOMPARE(result.lastResult.value(QStringLiteral("requestId")).toString(), requestId);
        QCOMPARE(result.lastResult.value(QStringLiteral("data")).toMap()
                     .value(QStringLiteral("answer")).toInt(), 42);
        QVERIFY(responder.firstResponseAccepted);
        QVERIFY(!responder.secondResponseAccepted);
    }

    void requestTimeoutCancelAndResponderUnloadAreDeterministic() {
        xuanhub::EventBus bus;
        xuanhub::PluginEventScope requester(&bus, QStringLiteral("requester"));
        xuanhub::PluginEventScope responder(
            &bus, QStringLiteral("responder"),
            {QStringLiteral("events.subscribe:plugin.requester.*")});
        EventReceiver sink;
        EventReceiver timeoutResult;
        QVERIFY(responder.subscribeV2(QStringLiteral("plugin.requester.timeout"), &sink,
                                      "onEnvelope(QVariantMap)"));
        QVERIFY(!requester.request(QStringLiteral("plugin.requester.timeout"), {},
                                   &timeoutResult, "onResult(QVariantMap)", 10).isEmpty());
        QTRY_COMPARE(timeoutResult.resultCalls, 1);
        QCOMPARE(timeoutResult.lastResult.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("timeout"));

        EventReceiver cancelResult;
        const QString cancelId = requester.request(
            QStringLiteral("plugin.requester.timeout"), {},
            &cancelResult, "onResult(QVariantMap)", 500);
        QVERIFY(requester.cancelRequest(cancelId));
        QTRY_COMPARE(cancelResult.resultCalls, 1);
        QCOMPARE(cancelResult.lastResult.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("canceled"));

        EventReceiver unloadResult;
        const QString unloadId = requester.request(
            QStringLiteral("plugin.requester.timeout"), {},
            &unloadResult, "onResult(QVariantMap)", 500);
        QVERIFY(!unloadId.isEmpty());
        responder.clear();
        QTRY_COMPARE(unloadResult.resultCalls, 1);
        QCOMPARE(unloadResult.lastResult.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("responder_unloaded"));
    }

    void clearingScopeCancelsAlreadyQueuedDelivery() {
        xuanhub::EventBus bus;
        xuanhub::PluginEventScope scope(&bus, QStringLiteral("demo"));
        EventReceiver receiver;
        QVERIFY(scope.subscribeV2(QStringLiteral("plugin.demo.queued"), &receiver,
                                  "onEnvelope(QVariantMap)"));
        QVERIFY(scope.publish(QStringLiteral("plugin.demo.queued")));
        scope.clear();
        QTest::qWait(20);
        QCOMPARE(receiver.calls, 0);
    }
};

QTEST_GUILESS_MAIN(EventBusTest)
#include "EventBusTest.moc"
