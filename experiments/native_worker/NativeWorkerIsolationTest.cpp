// NativeWorkerIsolationTest.cpp — 原生 Worker 协议、性能和故障隔离验证
#include "PyBridgeProtocol.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>
#include <QtTest>

#include <windows.h>
#include <psapi.h>

using xuanhub::pybridge::DecodeResult;
using xuanhub::pybridge::FrameCodec;
using xuanhub::pybridge::WorkerHello;

namespace {
class KillOnCloseJob final {
public:
    KillOnCloseJob() {
        m_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!m_job || !SetInformationJobObject(
                m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            close();
        }
    }
    ~KillOnCloseJob() { close(); }
    bool assign(qint64 processId) const {
        if (!m_job) return false;
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                     static_cast<DWORD>(processId));
        if (!process) return false;
        const bool ok = AssignProcessToJobObject(m_job, process) != FALSE;
        CloseHandle(process);
        return ok;
    }
private:
    void close() { if (m_job) CloseHandle(m_job); m_job = nullptr; }
    HANDLE m_job = nullptr;
};

class WorkerSession final {
public:
    bool start(const QString& expectedId = QStringLiteral("native_counter")) {
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces)
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XUANHUB_NATIVE_TOKEN"), m_token);
        process.setProcessEnvironment(environment);
        process.setProgram(QStringLiteral(XUANHUB_NATIVE_WORKER));
        process.setArguments({QStringLiteral("native_counter")});
        process.start();
        if (!process.waitForStarted(2000) || !job.assign(process.processId())) return false;
        QJsonObject helloMessage;
        if (!receive(&helloMessage, 2000)) return false;
        WorkerHello hello;
        QString error;
        return xuanhub::pybridge::parseWorkerHello(
            helloMessage, m_token, expectedId, &hello, &error)
            && hello.protocolVersion == 2
            && hello.capabilities.contains(QStringLiteral("nativeWorkerPrototype"));
    }

    bool request(const QString& method, const QJsonObject& args,
                 QJsonObject* response, int timeoutMs = 2000) {
        const qint64 id = ++m_requestId;
        const QByteArray frame = FrameCodec::encode({
            {QStringLiteral("type"), QStringLiteral("request")},
            {QStringLiteral("id"), id},
            {QStringLiteral("method"), method},
            {QStringLiteral("args"), args}});
        if (frame.isEmpty() || process.write(frame) != frame.size()
            || !process.waitForBytesWritten(timeoutMs) || !receive(response, timeoutMs)) {
            return false;
        }
        return response->value(QStringLiteral("id")).toInteger(-1) == id;
    }

    bool initialize(qint64 initial = 0) {
        QJsonObject reply;
        return request(QStringLiteral("init"), {{QStringLiteral("initial"), initial}}, &reply)
            && reply.value(QStringLiteral("ok")).toBool();
    }

    qint64 workingSetBytes() const {
        PROCESS_MEMORY_COUNTERS counters{};
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION
                                    | PROCESS_VM_READ, FALSE,
                                    static_cast<DWORD>(process.processId()));
        if (!handle) return -1;
        const bool ok = GetProcessMemoryInfo(handle, &counters, sizeof(counters)) != FALSE;
        CloseHandle(handle);
        return ok ? static_cast<qint64>(counters.WorkingSetSize) : -1;
    }

    void kill() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }
    }

    ~WorkerSession() { kill(); }
    QProcess process;

private:
    bool receive(QJsonObject* output, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QString error;
            const DecodeResult result = codec.takeNext(output, &error);
            if (result == DecodeResult::Ready) return true;
            if (result == DecodeResult::Error) return false;
            const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0 || !process.waitForReadyRead(remaining)) return false;
            codec.append(process.readAllStandardOutput());
        }
        return false;
    }

    QString m_token;
    qint64 m_requestId = 0;
    FrameCodec codec;
    KillOnCloseJob job;
};
} // namespace

class NativeWorkerIsolationTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void runsCounterOverSharedProtocol() {
        WorkerSession worker;
        QVERIFY(worker.start());
        QVERIFY(worker.initialize(4));
        QJsonObject reply;
        QVERIFY(worker.request(QStringLiteral("invoke"), {
            {QStringLiteral("name"), QStringLiteral("increment")},
            {QStringLiteral("amount"), 3}}, &reply));
        QVERIFY(reply.value(QStringLiteral("ok")).toBool());
        QCOMPARE(reply.value(QStringLiteral("result")).toInteger(), 7);
        QVERIFY(worker.request(QStringLiteral("update"), {}, &reply));
        QCOMPARE(reply.value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("counter")).toInteger(), 7);
    }

    void rejectsIdentityBeforeInitialization() {
        WorkerSession worker;
        QVERIFY(!worker.start(QStringLiteral("different_plugin")));
        worker.kill();
    }

    void crashDoesNotTerminateHostTest() {
        WorkerSession worker;
        QVERIFY(worker.start());
        QVERIFY(worker.initialize());
        const QByteArray frame = FrameCodec::encode({
            {QStringLiteral("type"), QStringLiteral("request")},
            {QStringLiteral("id"), 99},
            {QStringLiteral("method"), QStringLiteral("invoke")},
            {QStringLiteral("args"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("crash")}}}});
        QCOMPARE(worker.process.write(frame), qint64(frame.size()));
        QVERIFY(worker.process.waitForFinished(2000));
        QCOMPARE(worker.process.exitStatus(), QProcess::CrashExit);
        QVERIFY(true); // 若执行到这里，Worker 崩溃未进入宿主进程。
    }

    void hungWorkerCanBeTerminated() {
        WorkerSession worker;
        QVERIFY(worker.start());
        QVERIFY(worker.initialize());
        const QByteArray frame = FrameCodec::encode({
            {QStringLiteral("type"), QStringLiteral("request")},
            {QStringLiteral("id"), 100},
            {QStringLiteral("method"), QStringLiteral("invoke")},
            {QStringLiteral("args"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("hang")}}}});
        QCOMPARE(worker.process.write(frame), qint64(frame.size()));
        QVERIFY(worker.process.waitForBytesWritten(500));
        QVERIFY(!worker.process.waitForReadyRead(200));
        worker.kill();
        QCOMPARE(worker.process.state(), QProcess::NotRunning);
    }

    void measuresPrototypeOverhead() {
        WorkerSession worker;
        QElapsedTimer startup;
        startup.start();
        QVERIFY(worker.start());
        const qint64 startupMs = startup.elapsed();
        QVERIFY(worker.initialize());
        QElapsedTimer calls;
        calls.start();
        QJsonObject reply;
        constexpr int iterations = 500;
        for (int i = 0; i < iterations; ++i)
            QVERIFY(worker.request(QStringLiteral("update"), {}, &reply));
        const double averageMs = double(calls.nsecsElapsed()) / 1'000'000.0 / iterations;
        const qint64 workingSet = worker.workingSetBytes();
        qInfo().noquote() << QStringLiteral(
            "PF11_METRICS startupMs=%1 averageUpdateMs=%2 workingSetBytes=%3")
            .arg(startupMs).arg(averageMs, 0, 'f', 3).arg(workingSet);
        QVERIFY2(startupMs < 2000, "native worker startup exceeds prototype budget");
        QVERIFY2(averageMs < 20.0, "native worker update latency exceeds prototype budget");
        QVERIFY2(workingSet > 0 && workingSet < 100LL * 1024 * 1024,
                 "native worker working set exceeds prototype budget");
    }
};

QTEST_GUILESS_MAIN(NativeWorkerIsolationTest)
#include "NativeWorkerIsolationTest.moc"
