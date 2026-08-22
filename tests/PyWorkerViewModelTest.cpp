// PyWorkerViewModelTest.cpp — 宿主 Qt 代理到真实隔离 Python worker 的集成回归测试
#include "PyWorkerViewModel.h"
#include "barplugin/EventBus.h"
#include "barplugin/ProcessReaper.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

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
            PyWorkerViewModel viewModel(QStringLiteral("py_worker_test"), pluginDir,
                                        {{QStringLiteral("prefix"), QStringLiteral("isolated")},
                                         {QStringLiteral("stopFile"), stopFile}},
                                        python, worker, &bus);
            QVERIFY(viewModel.start());
            QTRY_VERIFY_WITH_TIMEOUT(viewModel.running(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("prefix")).toString(),
                                      QStringLiteral("isolated"), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("locale")).toString(),
                                      QStringLiteral("en-US"), 5000);
            bus.publish(QStringLiteral("app.locale.changed"),
                        {{QStringLiteral("locale"), QStringLiteral("zh-CN")}});
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("locale")).toString(),
                                      QStringLiteral("zh-CN"), 5000);
            QCOMPARE(viewModel.call(QStringLiteral("bump"), {3}).toMap()
                         .value(QStringLiteral("queued")).toBool(), true);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("count")).toInt(), 3, 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(stopFile), 3000);
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
};

QTEST_GUILESS_MAIN(PyWorkerViewModelTest)
#include "PyWorkerViewModelTest.moc"
