// AppUpdaterMain.cpp — 独立更新进程等待宿主退出、替换文件并验证新版启动
#include "AppUpdateManifest.h"
#include "AppUpdateTransaction.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <windows.h>
#include <tlhelp32.h>

namespace {
class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) : value(value) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE value;
};

QString native(const QString& path) { return QDir::toNativeSeparators(path); }

bool sameParent(HANDLE process, const QString& target) {
    wchar_t path[32768];
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) return false;
    return QFileInfo(QString::fromWCharArray(path, length)).canonicalFilePath().compare(
        QFileInfo(QDir(target).filePath(QStringLiteral("XuanHub.exe"))).canonicalFilePath(),
        Qt::CaseInsensitive) == 0;
}

bool targetIsRunning(const QString& target) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) return true;
    do {
        if (QString::fromWCharArray(entry.szExeFile).compare(QStringLiteral("XuanHub.exe"), Qt::CaseInsensitive) != 0)
            continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process.value || sameParent(process.value, target)) return true;
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}

bool startOld(const QString& target) {
    const QString exe = QDir(target).filePath(QStringLiteral("XuanHub.exe"));
    QString command = QLatin1Char('"') + native(exe) + QLatin1Char('"');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(native(exe).utf16()),
        reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
        reinterpret_cast<LPCWSTR>(native(target).utf16()), &startup, &process)) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool probeNew(AppUpdateTransaction& transaction, QString* error) {
    const QString executable = QDir(transaction.target()).filePath(QStringLiteral("XuanHub.exe"));
    const QString readyPath = QDir(transaction.directory()).filePath(QStringLiteral("app-ready.json"));
    QFile::remove(readyPath);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits))) {
        *error = appUpdateText("无法创建新版进程隔离组");
        return false;
    }
    QString command = QStringLiteral("\"%1\" --app-update-job \"%2\"")
        .arg(native(executable), native(transaction.directory()));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(reinterpret_cast<LPCWSTR>(native(executable).utf16()),
        reinterpret_cast<LPWSTR>(command.data()), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr, reinterpret_cast<LPCWSTR>(native(transaction.target()).utf16()), &startup, &process)) {
        *error = appUpdateText("新版进程无法启动：%1").arg(GetLastError());
        return false;
    }
    Handle child(process.hProcess);
    Handle thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 1);
        WaitForSingleObject(child.value, 10000);
        *error = appUpdateText("无法隔离新版进程，已取消启动");
        return false;
    }
    ResumeThread(thread.value);
    QElapsedTimer timer;
    timer.start();
    bool ready = false;
    while (timer.elapsed() < 45000) {
        if (WaitForSingleObject(child.value, 100) == WAIT_OBJECT_0) break;
        const QJsonObject receipt = readAppUpdateJson(readyPath);
        if (receipt.value(QStringLiteral("token")) == transaction.job().value(QStringLiteral("token"))
            && receipt.value(QStringLiteral("version")) == transaction.job().value(QStringLiteral("version"))
            && receipt.value(QStringLiteral("pid")).toInteger() == process.dwProcessId) {
            ready = true;
            break;
        }
    }
    if (ready && WaitForSingleObject(child.value, 2000) == WAIT_TIMEOUT) {
        // 验证成功后解除 kill-on-close，新宿主及其 worker 继续正常运行。
        limits.BasicLimitInformation.LimitFlags = 0;
        if (SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                   &limits, sizeof(limits))) return true;
    }
    TerminateJobObject(job.value, 1);
    WaitForSingleObject(child.value, 10000);
    QElapsedTimer draining;
    draining.start();
    while (draining.elapsed() < 10000) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(job.value, JobObjectBasicAccountingInformation,
            &accounting, sizeof(accounting), nullptr) || accounting.ActiveProcesses == 0) break;
        QThread::msleep(20);
    }
    *error = appUpdateText("新版未完成启动确认，正在恢复旧版");
    return false;
}

void showFailure(const QString& error, const QString& directory) {
    const QString message = error + appUpdateText("\n事务与备份目录：") + native(directory);
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"XuanHub 更新", MB_OK | MB_ICONERROR);
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 3 || (arguments[1] != QLatin1String("--apply")
        && arguments[1] != QLatin1String("--recover")
        && arguments[1] != QLatin1String("--recover-offline"))) return 2;
    AppUpdateTransaction transaction(arguments[2]);
    QString error;
    if (!transaction.load(&error)) return 2;
    const QString identity = QString::fromLatin1(QCryptographicHash::hash(
        transaction.target().toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString mutexName = QStringLiteral("Local\\XuanHubUpdate-") + identity;
    Handle mutex(CreateMutexW(nullptr, TRUE, reinterpret_cast<LPCWSTR>(mutexName.utf16())));
    if (!mutex.value || GetLastError() == ERROR_ALREADY_EXISTS) {
        writeAppUpdateJson(QDir(transaction.directory()).filePath(QStringLiteral("result.json")),
            {{QStringLiteral("status"), QStringLiteral("failed")},
             {QStringLiteral("message"), appUpdateText("已有更新器正在处理此目录")}}, nullptr);
        return 3;
    }

    const bool offline = arguments[1] == QLatin1String("--recover-offline");
    const bool recover = arguments[1] == QLatin1String("--recover") || offline;
    if (recover && !QFileInfo::exists(QDir(transaction.target()).filePath(QStringLiteral(".xuanhub-update.json")))) {
        const QString previous = readAppUpdateJson(QDir(transaction.directory()).filePath(
            QStringLiteral("result.json"))).value(QStringLiteral("status")).toString();
        if (previous == QLatin1String("updated") || previous == QLatin1String("rolled-back")) return 0;
    }
    const DWORD parentId = recover && arguments.size() == 4 ? arguments[3].toULong()
        : static_cast<DWORD>(transaction.job().value(QStringLiteral("parentPid")).toInteger());
    Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentId));
    if (offline && targetIsRunning(transaction.target())) return 4;
    if (!offline && (!parent.value || !sameParent(parent.value, transaction.target()))) {
        writeAppUpdateJson(QDir(transaction.directory()).filePath(QStringLiteral("result.json")),
            {{QStringLiteral("status"), QStringLiteral("failed")},
             {QStringLiteral("message"), appUpdateText("无法确认原宿主进程，未修改发行文件")}}, &error);
        return 4;
    }
    if (!recover) {
        if (!AppUpdateManifest::preflight(transaction.target(), transaction.stage(), &error)
            || !writeAppUpdateJson(QDir(transaction.target()).filePath(QStringLiteral(".xuanhub-update.json")),
                {{QStringLiteral("directory"), transaction.directory()},
                 {QStringLiteral("token"), transaction.job().value(QStringLiteral("token"))}}, &error)
            || !writeAppUpdateJson(QDir(transaction.directory()).filePath(QStringLiteral("helper-ready.json")),
                {{QStringLiteral("token"), transaction.job().value(QStringLiteral("token"))}}, &error)) {
            transaction.complete(QStringLiteral("failed"), error, nullptr);
            return 5;
        }
    }
    if (!offline && WaitForSingleObject(parent.value, 30000) != WAIT_OBJECT_0) {
        transaction.complete(QStringLiteral("failed"), appUpdateText("宿主未在时限内退出，更新已取消"), nullptr);
        return 6;
    }
    if (targetIsRunning(transaction.target())) {
        const QString message = appUpdateText("仍有玄枢进程占用此目录，请退出后恢复更新");
        writeAppUpdateJson(QDir(transaction.directory()).filePath(QStringLiteral("result.json")),
            {{QStringLiteral("status"), QStringLiteral("recovery-required")},
             {QStringLiteral("message"), message}}, nullptr);
        showFailure(message, transaction.directory());
        return 6;
    }
    bool success = false;
    if (!recover) success = transaction.apply(&error) && probeNew(transaction, &error);
    if (success) {
        transaction.complete(QStringLiteral("updated"), appUpdateText("软件更新成功"), nullptr);
        return 0;
    }
    const QString originalError = recover ? appUpdateText("已恢复中断的更新") : error;
    if (!transaction.rollback(&error)) {
        // 回滚不完整时保留标记，不能重启混合版本目录。
        writeAppUpdateJson(QDir(transaction.directory()).filePath(QStringLiteral("result.json")),
            {{QStringLiteral("status"), QStringLiteral("recovery-required")},
             {QStringLiteral("message"), originalError + appUpdateText("；恢复失败：") + error}}, nullptr);
        showFailure(error, transaction.directory());
        return 7;
    }
    transaction.complete(QStringLiteral("rolled-back"), originalError, nullptr);
    if (!startOld(transaction.target())) showFailure(appUpdateText("旧文件已恢复，但无法重新启动玄枢"), transaction.directory());
    return 1;
}
