// AppUpdateHostFixture.cpp — 隔离进程验收用宿主，覆盖退出握手与子进程失败
#include "AppUpdateManifest.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QTimer>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const QString target = QCoreApplication::applicationDirPath();
    if (arguments.size() >= 3 && arguments[1] == QLatin1String("--launch-update")) {
        const QString directory = arguments[2];
        const QString jobPath = QDir(directory).filePath(QStringLiteral("job.json"));
        QJsonObject job = readAppUpdateJson(jobPath);
        job.insert(QStringLiteral("parentPid"), QCoreApplication::applicationPid());
        if (!writeAppUpdateJson(jobPath, job, nullptr)) return 2;
        const QString helper = QDir(directory).filePath(QStringLiteral("helper/XuanHubUpdater.exe"));
        if (!QProcess::startDetached(helper, {QStringLiteral("--apply"), directory},
                                     QFileInfo(helper).absolutePath())) return 3;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 15000) {
            if (!readAppUpdateJson(QDir(directory).filePath(QStringLiteral("helper-ready.json"))).isEmpty()) return 0;
            if (!readAppUpdateJson(QDir(directory).filePath(QStringLiteral("result.json"))).isEmpty()) return 4;
            QThread::msleep(20);
        }
        return 5;
    }
    if (arguments.contains(QStringLiteral("--worker"))) {
        QTimer::singleShot(60000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (arguments.size() >= 3 && arguments[1] == QLatin1String("--app-update-job")) {
        if (QFileInfo::exists(QDir(target).filePath(QStringLiteral("fail-startup")))) {
            qint64 worker = 0;
            QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                    {QStringLiteral("--worker")}, target, &worker);
            writeAppUpdateJson(QDir(target).filePath(QStringLiteral("probe-worker.json")),
                               {{QStringLiteral("pid"), worker}}, nullptr);
            return 90;
        }
        const QString directory = arguments[2];
        const QJsonObject job = readAppUpdateJson(QDir(directory).filePath(QStringLiteral("job.json")));
        writeAppUpdateJson(QDir(directory).filePath(QStringLiteral("app-ready.json")),
            {{QStringLiteral("pid"), QCoreApplication::applicationPid()},
             {QStringLiteral("token"), job.value(QStringLiteral("token"))},
             {QStringLiteral("version"), job.value(QStringLiteral("version"))}}, nullptr);
        QTimer::singleShot(5000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    writeAppUpdateJson(QDir(target).filePath(QStringLiteral("old-restarted.json")),
                       {{QStringLiteral("ok"), true}}, nullptr);
    return 0;
}
