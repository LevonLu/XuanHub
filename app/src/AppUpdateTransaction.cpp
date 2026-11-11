// AppUpdateTransaction.cpp — 每次改动前落盘备份与日志，失败后按逆序恢复
#include "AppUpdateTransaction.h"
#include "AppUpdateManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool replaceFile(const QString& source, const QString& destination, QString* error) {
    // 未改变的文件可能仍被第三方占用；回滚无需重写相同内容，避免无谓失败。
    if (QFileInfo::exists(destination) && QFileInfo(source).size() == QFileInfo(destination).size()) {
        const QString sourceHash = AppUpdateManifest::hashFile(source);
        if (!sourceHash.isEmpty() && sourceHash == AppUpdateManifest::hashFile(destination)) return true;
    }
    QFile input(source);
    QSaveFile output(destination);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())
        || !input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return fail(error, appUpdateText("无法准备替换文件：%1").arg(destination));
    while (!input.atEnd()) {
        const QByteArray bytes = input.read(1024 * 1024);
        if ((bytes.isEmpty() && input.error() != QFile::NoError)
            || output.write(bytes) != bytes.size())
            return fail(error, appUpdateText("无法复制文件：%1").arg(destination));
    }
    if (!output.commit())
        return fail(error, appUpdateText("无法替换文件（权限或文件占用）：%1").arg(destination));
    return true;
}
}

AppUpdateTransaction::AppUpdateTransaction(QString jobDirectory)
    : m_directory(QDir::cleanPath(QFileInfo(jobDirectory).absoluteFilePath())) {}

QString AppUpdateTransaction::stage() const {
    return QDir(m_directory).filePath(QStringLiteral("stage"));
}

bool AppUpdateTransaction::load(QString* error) {
    m_job = readAppUpdateJson(QDir(m_directory).filePath(QStringLiteral("job.json")));
    m_target = m_job.value(QStringLiteral("target")).toString();
    const QString canonicalTarget = QFileInfo(m_target).canonicalFilePath();
    const QString canonicalJob = QFileInfo(m_directory).canonicalFilePath();
    if (m_job.value(QStringLiteral("schemaVersion")) != 1 || canonicalTarget.isEmpty()
        || canonicalJob.isEmpty() || !QFileInfo(m_target).isDir()
        || canonicalTarget.compare(canonicalJob, Qt::CaseInsensitive) == 0
        || canonicalJob.startsWith(canonicalTarget + QLatin1Char('/'), Qt::CaseInsensitive)
        || canonicalTarget.startsWith(canonicalJob + QLatin1Char('/'), Qt::CaseInsensitive)
        || m_job.value(QStringLiteral("token")).toString().size() != 64
        || !AppUpdateManifest::safeDestination(m_target, AppUpdateManifest::fileName())
        || !AppUpdateManifest::safeDestination(m_directory, QStringLiteral("stage"))
        || !AppUpdateManifest::safeDestination(m_directory, QStringLiteral("backup")))
        return fail(error, appUpdateText("更新事务路径或身份无效"));
    m_target = canonicalTarget;
    return true;
}

bool AppUpdateTransaction::apply(QString* error, const FaultInjector& fault) {
    if (!AppUpdateManifest::preflight(m_target, stage(), error)) return false;
    const QString journalPath = QDir(m_directory).filePath(QStringLiteral("journal.json"));
    if (QFileInfo::exists(journalPath))
        return fail(error, appUpdateText("已有更新日志，请先恢复上次事务"));
    AppUpdateManifest oldManifest, newManifest;
    if (!AppUpdateManifest::read(m_target, &oldManifest, error)
        || !AppUpdateManifest::read(stage(), &newManifest, error)) return false;
    if (oldManifest.version != m_job.value(QStringLiteral("oldVersion")).toString()
        || newManifest.version != m_job.value(QStringLiteral("version")).toString())
        return fail(error, appUpdateText("事务版本与发行清单不一致"));

    QMap<QString, QString> paths;
    for (auto it = oldManifest.files.cbegin(); it != oldManifest.files.cend(); ++it)
        paths.insert(it.key().toCaseFolded(), it.key());
    for (auto it = newManifest.files.cbegin(); it != newManifest.files.cend(); ++it)
        paths.insert(it.key().toCaseFolded(), it.key());
    paths.insert(AppUpdateManifest::fileName(), AppUpdateManifest::fileName());
    QJsonArray journal;
    int operations = 0;
    for (const QString& relative : paths) {
        const QString destination = QDir(m_target).filePath(relative);
        const QString backup = QDir(m_directory).filePath(QStringLiteral("backup/") + relative);
        const QString source = QDir(stage()).filePath(relative);
        if (!AppUpdateManifest::safeDestination(m_target, relative)
            || !AppUpdateManifest::safeDestination(m_directory, QStringLiteral("backup/") + relative))
            return fail(error, appUpdateText("事务路径变成链接：%1").arg(relative));
        const bool existed = QFileInfo::exists(destination);
        // 备份完整后才记“即将修改”；因此中断恢复不会拿不完整的备份覆盖原文件。
        if (existed && !replaceFile(destination, backup, error)) return false;
        journal.append(QJsonObject{{QStringLiteral("path"), relative},
                                   {QStringLiteral("existed"), existed}});
        if (!writeAppUpdateJson(journalPath,
            {{QStringLiteral("entries"), journal}}, error)) return false;
        if (QFileInfo::exists(source)) {
            if (!replaceFile(source, destination, error)) return false;
        } else if (existed && !QFile::remove(destination)) {
            return fail(error, appUpdateText("无法移除旧发行文件：%1").arg(relative));
        }
        if (fault && fault(++operations))
            return fail(error, appUpdateText("测试注入的替换失败"));
    }
    return AppUpdateManifest::verify(m_target, newManifest, error);
}

bool AppUpdateTransaction::rollback(QString* error) {
    const QJsonObject record = readAppUpdateJson(
        QDir(m_directory).filePath(QStringLiteral("journal.json")));
    if (record.isEmpty()) {
        if (QFileInfo::exists(QDir(m_directory).filePath(QStringLiteral("journal.json"))))
            return fail(error, appUpdateText("更新日志损坏，备份已保留"));
        return true;
    }
    if (!record.value(QStringLiteral("entries")).isArray())
        return fail(error, appUpdateText("更新日志损坏，备份已保留"));
    const QJsonArray entries = record.value(QStringLiteral("entries")).toArray();
    for (qsizetype index = entries.size(); index > 0; --index) {
        const QJsonObject entry = entries.at(index - 1).toObject();
        const QString relative = entry.value(QStringLiteral("path")).toString();
        if (!AppUpdateManifest::safeRelativePath(relative)
            || (AppUpdateManifest::protectedPath(relative)
                && relative != AppUpdateManifest::fileName())
            || !entry.value(QStringLiteral("existed")).isBool()
            || !AppUpdateManifest::safeDestination(m_target, relative)
            || !AppUpdateManifest::safeDestination(m_directory, QStringLiteral("backup/") + relative))
            return fail(error, appUpdateText("更新日志包含无效路径"));
        const QString destination = QDir(m_target).filePath(relative);
        if (entry.value(QStringLiteral("existed")).toBool()) {
            if (!replaceFile(QDir(m_directory).filePath(QStringLiteral("backup/") + relative),
                             destination, error)) return false;
        } else if (QFileInfo::exists(destination) && !QFile::remove(destination)) {
            return fail(error, appUpdateText("无法清理更新文件：%1").arg(relative));
        }
    }
    AppUpdateManifest restored;
    if (!AppUpdateManifest::read(m_target, &restored, error)
        || restored.version != m_job.value(QStringLiteral("oldVersion")).toString())
        return fail(error, appUpdateText("恢复后的发行清单与旧版不一致"));
    return AppUpdateManifest::verify(m_target, restored, error);
}

bool AppUpdateTransaction::complete(const QString& status, const QString& message, QString* error) {
    if (!writeAppUpdateJson(QDir(m_directory).filePath(QStringLiteral("result.json")),
        {{QStringLiteral("status"), status}, {QStringLiteral("message"), message}}, error)) return false;
    const QString marker = QDir(m_target).filePath(QStringLiteral(".xuanhub-update.json"));
    const QJsonObject pending = readAppUpdateJson(marker);
    if (pending.value(QStringLiteral("token")) == m_job.value(QStringLiteral("token"))
        && !QFile::remove(marker)) return fail(error, appUpdateText("无法移除更新标记"));
    return true;
}
