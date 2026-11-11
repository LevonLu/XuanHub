// AppUpdatePackage.cpp — 只解压清单中的普通文件，拒绝越界、链接和额外载荷
#include "AppUpdatePackage.h"
#include "AppUpdateManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QtCore/private/qzipreader_p.h>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
}

bool AppUpdatePackage::extract(const QString& archive, const QString& stage,
                               const QString& version, const std::atomic_bool& cancelled,
                               QString* error) {
    if (QFileInfo::exists(stage) || !QDir().mkpath(stage))
        return fail(error, appUpdateText("暂存目录已存在或无法创建"));
    QZipReader reader(archive);
    if (reader.status() != QZipReader::NoError)
        return fail(error, appUpdateText("无法读取更新 ZIP"));
    const auto entries = reader.fileInfoList();
    if (entries.isEmpty() || entries.size() > 150000)
        return fail(error, appUpdateText("ZIP 条目数量无效"));
    QSet<QString> names;
    QSet<QString> regularNames;
    qint64 total = 0;
    bool hasManifest = false;
    for (const auto& entry : entries) {
        QString path = entry.filePath;
        if (entry.isDir && path.endsWith(QLatin1Char('/'))) path.chop(1);
        if (!AppUpdateManifest::safeRelativePath(path) || entry.isSymLink
            || (!entry.isDir && !entry.isFile) || names.contains(path.toCaseFolded())
            || entry.size < 0 || entry.size > AppUpdateManifest::MaximumFileBytes)
            return fail(error, appUpdateText("ZIP 包含无效或重复路径：%1").arg(path));
        names.insert(path.toCaseFolded());
        total += entry.size;
        if (total > AppUpdateManifest::MaximumTotalBytes)
            return fail(error, appUpdateText("ZIP 解压大小超过限制"));
        if (entry.isFile) regularNames.insert(path);
        if (path == AppUpdateManifest::fileName() && entry.isFile) {
            hasManifest = true;
            if (entry.size > 16 * 1024 * 1024)
                return fail(error, appUpdateText("发行清单过大"));
        }
    }
    AppUpdateManifest manifest;
    if (QStorageInfo(stage).bytesAvailable() < total + 16 * 1024 * 1024)
        return fail(error, appUpdateText("可用磁盘空间不足以解压更新包"));
    if (!hasManifest || !AppUpdateManifest::parse(
            reader.fileData(AppUpdateManifest::fileName()), &manifest, error))
        return fail(error, appUpdateText("更新包缺少有效发行清单"));
    if (manifest.version != version)
        return fail(error, appUpdateText("更新包版本与 Release 不一致"));
    QSet<QString> expected = {AppUpdateManifest::fileName()};
    for (auto it = manifest.files.cbegin(); it != manifest.files.cend(); ++it) expected.insert(it.key());
    // 发行包中的默认配置与便携标记经过打包门禁，但升级时绝不写入用户目录。
    regularNames.remove(QStringLiteral("config.json"));
    regularNames.remove(QStringLiteral("portable"));
    if (regularNames != expected)
        return fail(error, appUpdateText("ZIP 文件集合与发行清单不一致"));
    for (const QString& path : expected) {
        if (cancelled.load()) return fail(error, appUpdateText("更新已取消"));
        if (!AppUpdateManifest::safeDestination(stage, path))
            return fail(error, appUpdateText("暂存路径包含链接"));
        const QByteArray bytes = reader.fileData(path);
        if (path != AppUpdateManifest::fileName()) {
            const AppUpdateFile file = manifest.files.value(path);
            if (bytes.size() != file.size || QString::fromLatin1(QCryptographicHash::hash(
                    bytes, QCryptographicHash::Sha256).toHex()) != file.sha256)
                return fail(error, appUpdateText("更新文件校验失败：%1").arg(path));
        }
        const QString destination = QDir(stage).filePath(path);
        QFile output(destination);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())
            || !output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
            return fail(error, appUpdateText("无法写入暂存文件：%1").arg(path));
    }
    if (reader.status() != QZipReader::NoError)
        return fail(error, appUpdateText("ZIP 数据损坏"));
    return true;
}
