// PluginDiagnosticsTest.cpp — 插件日志过滤与诊断包脱敏回归测试
#include "PluginDiagnostics.h"
#include "barplugin/PluginLog.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class PluginDiagnosticsTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void formatsScopedSingleLineLogs() {
        QCOMPARE(xuanhub::PluginLog::format(
                     QStringLiteral("py_demo"), QStringLiteral("worker.timeout"),
                     QStringLiteral("line one\nline two")),
                 QStringLiteral(
                     "[Plugin:py_demo] [code=worker.timeout] line one\\nline two"));
        QVERIFY(xuanhub::PluginLog::format(
                    QStringLiteral("bad id"), QStringLiteral("bad"), QStringLiteral("message"))
                    .startsWith(QStringLiteral(
                        "[Plugin:unknown] [code=plugin.log.message]")));
    }

    void filtersByPluginAndKeepsStableErrorCodes() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString logPath = temporary.filePath(QStringLiteral("xuanhub.log"));
        QFile log(logPath);
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Text));
        log.write("[2026-08-22 10:00:00.000] [W] [Plugin:py_demo] [worker.timeout] Bearer abcdefghijklmnop C:\\Users\\Alice\\secret.txt\n");
        log.write("[2026-08-22 10:00:01.000] [D] [PyBridge] py_demo update complete\n");
        log.write("[2026-08-22 10:00:02.000] [W] [PyBridge] py_other token=do-not-export\n");
        log.close();

        const QJsonArray entries = PluginDiagnostics::filteredLogEntries(
            logPath, QStringLiteral("py_demo"), {});
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).toObject().value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("worker.timeout"));
        QCOMPARE(entries.at(1).toObject().value(QStringLiteral("errorCode")).toString(),
                 QStringLiteral("plugin.log.debug"));
        const QByteArray serialized = QJsonDocument(entries).toJson();
        QVERIFY(!serialized.contains("Alice"));
        QVERIFY(!serialized.contains("abcdefghijklmnop"));
        QVERIFY(!serialized.contains("py_other"));
    }

    void writesOnlyRedactedDiagnosticData() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString privateRoot = temporary.filePath(QStringLiteral("private"));
        const QString logPath = temporary.filePath(QStringLiteral("xuanhub.log"));
        QFile log(logPath);
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Text));
        log.write(QStringLiteral("[2026-08-22 10:00:00.000] [W] [Plugin] py_demo failed at %1 api_key=sk-live-secretvalue\n")
                      .arg(privateRoot).toUtf8());
        log.close();

        const QString outputPath = temporary.filePath(QStringLiteral("diagnostics.json"));
        const PluginDiagnosticsResult result = PluginDiagnostics::write(
            outputPath, logPath,
            {{QStringLiteral("id"), QStringLiteral("py_demo")},
             {QStringLiteral("version"), QStringLiteral("1.2.3")}},
            {{QStringLiteral("state"), QStringLiteral("failed")},
             {QStringLiteral("lastError"),
              QStringLiteral("password=hunter2 at %1").arg(privateRoot)}},
            {{QStringLiteral("managed"), true},
             {QStringLiteral("version"), QStringLiteral("1.2.3")},
             {QStringLiteral("payloadDirectory"), privateRoot}},
            {privateRoot}, QStringLiteral("0.3.0"));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.logEntryCount, 1);

        QFile output(outputPath);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray bytes = output.readAll();
        QVERIFY(!bytes.contains("hunter2"));
        QVERIFY(!bytes.contains("secretvalue"));
        QVERIFY(!bytes.contains(privateRoot.toUtf8()));
        QVERIFY(!bytes.contains("payloadDirectory"));
        const QJsonObject document = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(document.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QVERIFY(document.value(QStringLiteral("redaction")).toObject()
                    .value(QStringLiteral("applied")).toBool());
        QVERIFY(!document.value(QStringLiteral("redaction")).toObject()
                     .value(QStringLiteral("configurationIncluded")).toBool());
    }
};

QTEST_GUILESS_MAIN(PluginDiagnosticsTest)
#include "PluginDiagnosticsTest.moc"
