// PyWorkerViewModelTest.cpp — 宿主 Qt 代理到真实隔离 Python worker 的集成回归测试
#include "PyWorkerViewModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class PyWorkerViewModelTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
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
state = {"count": 0, "prefix": "", "stopFile": ""}
def init(config):
    state["prefix"] = config.get("prefix", "")
    state["stopFile"] = config.get("stopFile", "")
def update():
    return {"count": state["count"], "prefix": state["prefix"]}
def invoke(name, args):
    if name == "bump": state["count"] += int(args[0])
    return state["count"]
def stop():
    Path(state["stopFile"]).write_text("stopped", encoding="utf-8")
)PY"));
        mainFile.close();

        {
            PyWorkerViewModel viewModel(QStringLiteral("py_worker_test"), pluginDir,
                                        {{QStringLiteral("prefix"), QStringLiteral("isolated")},
                                         {QStringLiteral("stopFile"), stopFile}},
                                        python, worker);
            QVERIFY(viewModel.start());
            QTRY_VERIFY_WITH_TIMEOUT(viewModel.running(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("prefix")).toString(),
                                      QStringLiteral("isolated"), 5000);
            QCOMPARE(viewModel.call(QStringLiteral("bump"), {3}).toMap()
                         .value(QStringLiteral("queued")).toBool(), true);
            QTRY_COMPARE_WITH_TIMEOUT(viewModel.values().value(QStringLiteral("count")).toInt(), 3, 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(stopFile), 3000);
    }
};

QTEST_GUILESS_MAIN(PyWorkerViewModelTest)
#include "PyWorkerViewModelTest.moc"
