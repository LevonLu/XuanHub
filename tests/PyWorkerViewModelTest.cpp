// PyWorkerViewModelTest.cpp — 宿主 Qt 代理到真实隔离 Python worker 的集成回归测试
#include "PyWorkerViewModel.h"
#include "PluginHostServices.h"
#include "barplugin/EventBus.h"
#include "barplugin/PluginEventScope.h"
#include "barplugin/ProcessReaper.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class PythonBusResponder final : public QObject {
    Q_OBJECT
public:
    explicit PythonBusResponder(xuanhub::EventBus* bus) : bus(bus) {}

public Q_SLOTS:
    void onRequest(QVariantMap envelope) {
        bus->respond(envelope.value(QStringLiteral("requestId")).toString(),
                     {{QStringLiteral("answer"), 42}});
    }

private:
    xuanhub::EventBus* bus;
};

class PyWorkerViewModelTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void normalizesUpdateInterval() {
        PyWorkerViewModel defaults(QStringLiteral("defaults"), {}, {}, {}, {}, nullptr);
        QCOMPARE(defaults.updateIntervalMs(), 1000);

        PyWorkerViewModel minimum(QStringLiteral("minimum"), {},
                                  {{QStringLiteral("intervalMs"), 1}}, {}, {}, nullptr);
        QCOMPARE(minimum.updateIntervalMs(), 100);

        PyWorkerViewModel maximum(QStringLiteral("maximum"), {},
                                  {{QStringLiteral("intervalMs"), 120000}}, {}, {}, nullptr);
        QCOMPARE(maximum.updateIntervalMs(), 60000);

        PyWorkerViewModel invalid(QStringLiteral("invalid"), {},
                                  {{QStringLiteral("intervalMs"), QStringLiteral("oops")}},
                                  {}, {}, nullptr);
        QCOMPARE(invalid.updateIntervalMs(), 1000);
    }

    void opensCircuitAfterFiveFailuresWithinWindow() {
        PyWorkerViewModel viewModel(QStringLiteral("circuit"), {}, {}, {}, {}, nullptr);

        for (int failure = 1; failure < 5; ++failure) {
            QVERIFY(!viewModel.recordWorkerFailure(failure * 1000));
            QCOMPARE(viewModel.consecutiveFailures(), failure);
            QVERIFY(!viewModel.circuitOpen());
        }
        QVERIFY(viewModel.recordWorkerFailure(5000));
        QCOMPARE(viewModel.consecutiveFailures(), 5);
        QVERIFY(viewModel.circuitOpen());
        QVERIFY(!viewModel.start());
    }

    void expiresOldFailuresAndClearsFailuresAfterSuccess() {
        PyWorkerViewModel viewModel(QStringLiteral("failure_window"), {}, {}, {}, {}, nullptr);

        QVERIFY(!viewModel.recordWorkerFailure(0));
        QVERIFY(!viewModel.recordWorkerFailure(10 * 60 * 1000 + 1));
        QCOMPARE(viewModel.consecutiveFailures(), 1);

        QVERIFY(!viewModel.recordWorkerFailure(10 * 60 * 1000 + 2));
        QCOMPARE(viewModel.consecutiveFailures(), 2);
        viewModel.markWorkerHealthy();
        QCOMPARE(viewModel.consecutiveFailures(), 0);
        QVERIFY(!viewModel.circuitOpen());
    }

    void manualRestartClearsOpenCircuit() {
        PyWorkerViewModel viewModel(QStringLiteral("manual_restart"), {}, {}, {}, {}, nullptr);
        for (int failure = 1; failure <= 5; ++failure)
            viewModel.recordWorkerFailure(failure * 1000);
        QVERIFY(viewModel.circuitOpen());

        QVERIFY(viewModel.restart());
        QVERIFY(!viewModel.circuitOpen());
        QCOMPARE(viewModel.consecutiveFailures(), 0);
    }

    void exposesHealthSnapshot() {
        PyWorkerViewModel viewModel(QStringLiteral("health_snapshot"), {}, {}, {}, {}, nullptr);
        QCOMPARE(viewModel.health().value(QStringLiteral("state")).toString(),
                 QStringLiteral("discovered"));
        for (int failure = 1; failure <= 5; ++failure)
            viewModel.recordWorkerFailure(failure * 1000);
        const QVariantMap health = viewModel.health();
        QCOMPARE(health.value(QStringLiteral("state")).toString(),
                 QStringLiteral("circuitOpen"));
        QCOMPARE(health.value(QStringLiteral("consecutiveFailures")).toInt(), 5);
    }

    void cancelsOnlyQueuedCalls() {
        PyWorkerViewModel viewModel(QStringLiteral("cancel_calls"), {}, {}, {}, {}, nullptr);
        viewModel.m_authenticated = true;
        viewModel.m_inFlightId = 1;
        viewModel.m_inFlight = {
            QStringLiteral("invoke"), {}, QStringLiteral("call-running")};
        viewModel.enqueue({
            QStringLiteral("invoke"), {}, QStringLiteral("call-queued")});
        QSignalSpy finished(&viewModel, &PyWorkerViewModel::callFinished);

        const QVariantMap running = viewModel.cancelCall(QStringLiteral("call-running"));
        QVERIFY(!running.value(QStringLiteral("canceled")).toBool());
        QCOMPARE(running.value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("not_interruptible"));

        const QVariantMap queued = viewModel.cancelCall(QStringLiteral("call-queued"));
        QVERIFY(queued.value(QStringLiteral("canceled")).toBool());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("call-queued"));
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(finished.at(0).at(3).toMap().value(QStringLiteral("code")).toString(),
                 QStringLiteral("canceled"));
    }

    void reaperGraceTimerSurvivesEarlyWorkerExit() {
        // 两个 worker 在 750ms 宽限期内先后自行退出，覆盖曾导致热重载闪退的
        // finished/deleteLater 与 grace timer 竞态。旧实现会在这里访问悬空 QProcess。
        for (int i = 0; i < 2; ++i) {
            auto* process = new QProcess;
            process->start(QStringLiteral("cmd.exe"),
                           {QStringLiteral("/d"), QStringLiteral("/c"),
                            QStringLiteral("ping -n 2 127.0.0.1 >nul")});
            QVERIFY(process->waitForStarted(3000));
            xuanhub::reapProcess(process, 1500);
        }
        QTest::qWait(2800);
    }

    void startUpdateInvokeAndStop() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");
        QVERIFY(QFileInfo(python).isFile());
        QVERIFY(QFileInfo(worker).isFile());

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        const QString stopFile = temporary.filePath(QStringLiteral("stopped.txt"));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
from pathlib import Path
state = {"count": 0, "prefix": "", "stopFile": "", "locale": "unset"}
def init(config):
    state["prefix"] = config.get("prefix", "")
    state["stopFile"] = config.get("stopFile", "")
def set_locale(locale):
    state["locale"] = locale
def set_config(config):
    state["prefix"] = config.get("prefix", "")
def validate_config(config):
    ok = config.get("prefix") != "blocked"
    return {"ok": ok, "errors": [] if ok else ["prefix blocked"]}
def update():
    return {"count": state["count"], "prefix": state["prefix"], "locale": state["locale"]}
def invoke(name, args):
    if name == "bump": state["count"] += int(args[0])
    return state["count"]
def stop():
    Path(state["stopFile"]).write_text("stopped", encoding="utf-8")
)PY"));
        mainFile.close();

        {
            xuanhub::EventBus bus;
            QVERIFY(bus.setState(QStringLiteral("app.locale"),
                                 {{QStringLiteral("locale"), QStringLiteral("zh-CN")}}));
            PyWorkerViewModel viewModel(QStringLiteral("py_worker_test"), pluginDir,
                                        {{QStringLiteral("prefix"), QStringLiteral("isolated")},
                                         {QStringLiteral("stopFile"), stopFile}},
                                        python, worker, &bus);
            QVERIFY(viewModel.start());
            QTRY_VERIFY_WITH_TIMEOUT(viewModel.running(), 5000);
            QCOMPARE(viewModel.protocolVersion(), 2);
            QVERIFY(viewModel.protocolCapabilities().contains(
                QStringLiteral("requestResponseV1")));
            QCOMPARE(viewModel.health().value(QStringLiteral("protocolVersion")).toInt(), 2);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("prefix")).toString(),
                                      QStringLiteral("isolated"), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("locale")).toString(),
                                      QStringLiteral("zh-CN"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(viewModel.supportsHotConfig(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(viewModel.supportsConfigValidation(), 5000);
            const auto accepted = viewModel.validateConfig(
                {{QStringLiteral("prefix"), QStringLiteral("allowed")}}, 5000);
            QVERIFY(accepted.supported);
            QVERIFY(accepted.valid);
            const auto rejected = viewModel.validateConfig(
                {{QStringLiteral("prefix"), QStringLiteral("blocked")}}, 5000);
            QVERIFY(rejected.supported);
            QVERIFY(!rejected.valid);
            QCOMPARE(rejected.error, QStringLiteral("prefix blocked"));
            QCOMPARE(viewModel.values().value(QStringLiteral("prefix")).toString(),
                     QStringLiteral("isolated"));
            QVERIFY(viewModel.applyConfig({
                {QStringLiteral("prefix"), QStringLiteral("hot")},
                {QStringLiteral("stopFile"), stopFile},
                {QStringLiteral("intervalMs"), 250},
            }));
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("prefix")).toString(),
                                      QStringLiteral("hot"), 5000);
            QCOMPARE(viewModel.updateIntervalMs(), 250);
            bus.setState(QStringLiteral("app.locale"),
                         {{QStringLiteral("locale"), QStringLiteral("en-US")}});
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("locale")).toString(),
                                      QStringLiteral("en-US"), 5000);
            QSignalSpy callFinished(&viewModel, &PyWorkerViewModel::callFinished);
            const QVariantMap call = viewModel.call(QStringLiteral("bump"), {3}).toMap();
            QVERIFY(call.value(QStringLiteral("queued")).toBool());
            QVERIFY(!call.value(QStringLiteral("id")).toString().isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(callFinished.size(), 1, 5000);
            QCOMPARE(callFinished.at(0).at(0).toString(),
                     call.value(QStringLiteral("id")).toString());
            QVERIFY(callFinished.at(0).at(1).toBool());
            QCOMPARE(callFinished.at(0).at(2).toInt(), 3);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("count")).toInt(), 3, 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(stopFile), 3000);
    }

    void routesHostServicesWithBoundPluginIdentity() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
from xuanhub_host import log, notify
state = {"notified": False, "error": ""}
def init(config):
    log("info", "python service ready", {"phase": "init"})
    result = notify("Python", "Ready")
    state["notified"] = result["success"]
    state["error"] = result["errorCode"]
def update():
    return state
)PY"));
        mainFile.close();

        xuanhub::EventBus bus;
        QSignalSpy dispatched(&bus, &xuanhub::EventBus::dispatched);
        PluginHostServices::Environment environment;
        environment.eventBus = &bus;
        environment.configDir = temporary.path();
        PluginHostServices services(
            QStringLiteral("py_bound_identity"),
            xuanhub::HostPluginTrustLevel::Isolated,
            {QStringLiteral("notifications")}, {QStringLiteral("notifications")},
            std::move(environment));

        PyWorkerViewModel viewModel(QStringLiteral("py_bound_identity"), pluginDir,
                                    {}, python, worker, &bus);
        viewModel.setHostServices(&services);
        QVERIFY(viewModel.start());
        QTRY_COMPARE_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("notified")).toBool(), true, 5000);
        QCOMPARE(viewModel.values().value(QStringLiteral("error")).toString(), QString{});

        bool foundNotification = false;
        for (const auto& arguments : dispatched) {
            if (arguments.at(0).toString() != QLatin1String("app.notify")) continue;
            foundNotification = true;
            QCOMPARE(arguments.at(1).toMap().value(QStringLiteral("source")).toString(),
                     QStringLiteral("py_bound_identity"));
        }
        QVERIFY(foundNotification);
    }

    void deniesUndeclaredPythonNotificationPermission() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
from xuanhub_host import notify
state = {"success": True, "error": ""}
def init(config):
    result = notify("Denied", "Must not show")
    state["success"] = result["success"]
    state["error"] = result["errorCode"]
def update():
    return state
)PY"));
        mainFile.close();

        xuanhub::EventBus bus;
        PluginHostServices::Environment environment;
        environment.eventBus = &bus;
        environment.configDir = temporary.path();
        PluginHostServices services(
            QStringLiteral("py_denied"), xuanhub::HostPluginTrustLevel::Isolated,
            {}, {}, std::move(environment));
        PyWorkerViewModel viewModel(QStringLiteral("py_denied"), pluginDir,
                                    {}, python, worker, &bus);
        viewModel.setHostServices(&services);
        QVERIFY(viewModel.start());
        QTRY_COMPARE_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("error")).toString(),
            QStringLiteral("permission_denied"), 5000);
        QCOMPARE(viewModel.values().value(QStringLiteral("success")).toBool(), false);
    }

    void bridgesPythonPublishSubscribeAndRequest() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
from xuanhub_host import events
state = {"received": 0, "answer": 0, "published": False}
def on_message(envelope):
    state["received"] = envelope.get("data", {}).get("value", 0)
def init(config):
    events.subscribe("plugin.peer.message", on_message)
    state["published"] = events.publish("plugin.py_events.ready", {"ready": True})["success"]
def invoke(name, args):
    if name == "query":
        result = events.request("plugin.py_events.query", {"question": 1}, 1000)
        state["answer"] = result.get("data", {}).get("answer", 0)
def update():
    return dict(state)
)PY"));
        mainFile.close();

        xuanhub::EventBus bus;
        PythonBusResponder responder(&bus);
        QVERIFY(bus.subscribeV2(QStringLiteral("plugin.py_events.query"), &responder,
                                "onRequest(QVariantMap)"));
        xuanhub::PluginEventScope pythonScope(
            &bus, QStringLiteral("py_events"),
            {QStringLiteral("events.subscribe:plugin.peer.*")});
        PyWorkerViewModel viewModel(QStringLiteral("py_events"), pluginDir,
                                    {}, python, worker, &bus);
        viewModel.setEventScope(&pythonScope);
        QVERIFY(viewModel.start());
        QTRY_COMPARE_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("published")).toBool(), true, 5000);

        xuanhub::PluginEventScope peerScope(&bus, QStringLiteral("peer"));
        QVERIFY(peerScope.publish(QStringLiteral("plugin.peer.message"),
                                  {{QStringLiteral("value"), 9}}));
        QTRY_COMPARE_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("received")).toInt(), 9, 5000);

        QCOMPARE(viewModel.call(QStringLiteral("query"), {}).toMap()
                     .value(QStringLiteral("queued")).toBool(), true);
        QTRY_COMPARE_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("answer")).toInt(), 42, 5000);
    }

    void respectsConfiguredUpdateInterval() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
count = 0
def update():
    global count
    count += 1
    return {"count": count}
)PY"));
        mainFile.close();

        PyWorkerViewModel viewModel(QStringLiteral("py_interval_test"), pluginDir,
                                    {{QStringLiteral("intervalMs"), 400}},
                                    python, worker, nullptr);
        QVERIFY(viewModel.start());
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("count")).toInt() >= 1,
                                 5000);

        const int firstCount = viewModel.values().value(QStringLiteral("count")).toInt();
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::qWait(150);
        QCOMPARE(viewModel.values().value(QStringLiteral("count")).toInt(), firstCount);

        QTRY_VERIFY_WITH_TIMEOUT(
            viewModel.values().value(QStringLiteral("count")).toInt() > firstCount, 1200);
        QVERIFY2(elapsed.elapsed() >= 300,
                 "The next update ran before the configured interval elapsed");
    }

    void retainsLastSuccessfulValuesWhileRestarting() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString worker = qEnvironmentVariable("XUANHUB_TEST_WORKER");
        QVERIFY2(!python.isEmpty() && !worker.isEmpty(),
                 "CTest must provide python executable and bridge_worker.py paths");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString pluginDir = temporary.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(pluginDir));
        QFile mainFile(pluginDir + QStringLiteral("/main.py"));
        QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(mainFile.write(R"PY(
import os
count = 0
def update():
    global count
    count += 1
    if count > 1:
        os._exit(7)
    return {"status": "last-good", "count": count}
)PY"));
        mainFile.close();

        PyWorkerViewModel viewModel(QStringLiteral("py_last_value_test"), pluginDir,
                                    {{QStringLiteral("intervalMs"), 100}},
                                    python, worker, nullptr);
        QVERIFY(viewModel.start());
        QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("status")).toString(),
                                  QStringLiteral("last-good"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.running(), 3000);
        QCOMPARE(viewModel.values().value(QStringLiteral("status")).toString(),
                 QStringLiteral("last-good"));
        QCOMPARE(viewModel.values().value(QStringLiteral("count")).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(PyWorkerViewModelTest)
#include "PyWorkerViewModelTest.moc"
