// AppUpdateManifest.cpp — 严格限定官方文件集合，拒绝路径别名与用户文件覆盖
#include "AppUpdateManifest.h"
#include "SemanticVersion.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <windows.h>

namespace {
bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
}

QString AppUpdateManifest::fileName() { return QStringLiteral("update-manifest.json"); }

QString appUpdateText(const char* source) {
    return QCoreApplication::translate("xuanhub.update", source);
}

bool AppUpdateManifest::safeRelativePath(const QString& path) {
    static const QRegularExpression invalid(QStringLiteral(R"([<>:"\\|?*\x00-\x1f])"));
    static const QRegularExpression device(QStringLiteral(
        R"(^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$))"),
        QRegularExpression::CaseInsensitiveOption);
    if (path.isEmpty() || path.size() > 512 || path.startsWith(QLatin1Char('/'))
        || invalid.match(path).hasMatch()) return false;
    for (const QString& part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")
            || part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))
            || device.match(part).hasMatch()) return false;
    }
    return true;
}

bool AppUpdateManifest::protectedPath(const QString& path) {
    const QString name = path.toLower();
    const QString root = name.section(QLatin1Char('/'), 0, 0);
    static const QSet<QString> protectedRoots = {
        QStringLiteral("config.json"), QStringLiteral("secrets.json"),
        QStringLiteral("portable"), QStringLiteral("plugin-data"),
        QStringLiteral("plugin-store"), QStringLiteral("python-envs"),
        QStringLiteral("plugin-update-cache"), QStringLiteral("app-update-cache"),
        QStringLiteral("app-update-state.json"), QStringLiteral(".xuanhub-update.json"),
        QStringLiteral("xuanhub.lock"), QStringLiteral("xuanhub.log"),
        QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"),
        QStringLiteral(".env")};
    return protectedRoots.contains(root) || name == fileName()
        || name.endsWith(QStringLiteral(".lock")) || name.endsWith(QStringLiteral(".log"));
}

bool AppUpdateManifest::safeDestination(const QString& root, const QString& relative) {
    if (!safeRelativePath(relative)) return false;
    QString path = QFileInfo(root).absoluteFilePath();
    // QFileInfo 的符号链接判断不覆盖所有 Windows junction；每一级都检查 reparse 标志。
    const auto noReparse = [](const QString& candidate) {
        const DWORD attributes = GetFileAttributesW(
            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(candidate).utf16()));
        return attributes == INVALID_FILE_ATTRIBUTES
            || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    };
    for (QString parent = path; !parent.isEmpty();) {
        if (!noReparse(parent)) return false;
        const QString next = QFileInfo(parent).absolutePath();
        if (next == parent) break;
        parent = next;
    }
    for (const QString& part : relative.split(QLatin1Char('/'))) {
        path = QDir(path).filePath(part);
        if (!noReparse(path)) return false;
    }
    return true;
}

QString AppUpdateManifest::hashFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool AppUpdateManifest::parse(const QByteArray& bytes, AppUpdateManifest* result,
                              QString* error) {
    if (bytes.size() > 16 * 1024 * 1024) return fail(error, appUpdateText("发行清单过大"));
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    const QJsonObject object = document.object();
    AppUpdateManifest manifest;
    manifest.version = object.value(QStringLiteral("version")).toString();
    SemanticVersion version;
    if (!document.isObject() || object.value(QStringLiteral("schemaVersion")) != 1
        || !SemanticVersion::parse(manifest.version, &version) || !version.prerelease.isEmpty()
        || !object.value(QStringLiteral("files")).isArray())
        return fail(error, appUpdateText("发行清单格式或版本无效"));
    const QJsonArray files = object.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || files.size() > 100000)
        return fail(error, appUpdateText("发行清单文件数量无效"));
    static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
    QSet<QString> names;
    qint64 total = 0;
    for (const QJsonValue& value : files) {
        const QJsonObject entry = value.toObject();
        const QString path = entry.value(QStringLiteral("path")).toString();
        const QString hash = entry.value(QStringLiteral("sha256")).toString();
        const double size = entry.value(QStringLiteral("size")).toDouble(-1);
        if (!safeRelativePath(path) || protectedPath(path)
            || names.contains(path.toCaseFolded()) || !digest.match(hash).hasMatch()
            || !std::isfinite(size) || size < 0 || size > MaximumFileBytes
            || std::floor(size) != size)
            return fail(error, appUpdateText("发行清单文件无效：%1").arg(path));
        names.insert(path.toCaseFolded());
        total += static_cast<qint64>(size);
        if (total > MaximumTotalBytes) return fail(error, appUpdateText("发行包解压大小超过限制"));
        manifest.files.insert(path, {static_cast<qint64>(size), hash});
    }
    for (const QString& name : names) {
        QString parent = name;
        while (parent.contains(QLatin1Char('/'))) {
            parent = parent.left(parent.lastIndexOf(QLatin1Char('/')));
            if (names.contains(parent))
                return fail(error, appUpdateText("发行清单文件与目录冲突：%1").arg(name));
        }
    }
    for (const QString& required : {QStringLiteral("XuanHub.exe"),
                                    QStringLiteral("XuanHubUpdater.exe"),
                                    QStringLiteral("Qt6Core.dll"), QStringLiteral("barplugin.dll")}) {
        if (!manifest.files.contains(required))
            return fail(error, appUpdateText("发行清单缺少 %1").arg(required));
    }
    if (result) *result = manifest;
    return true;
}

bool AppUpdateManifest::read(const QString& root, AppUpdateManifest* result, QString* error) {
    QFile file(QDir(root).filePath(fileName()));
    if (!safeDestination(root, fileName()) || !file.open(QIODevice::ReadOnly)
        || file.size() > 16 * 1024 * 1024)
        return fail(error, appUpdateText("缺少有效发行清单，请使用支持一键更新的正式便携包"));
    return parse(file.readAll(), result, error);
}

bool AppUpdateManifest::verify(const QString& root, const AppUpdateManifest& manifest,
                               QString* error) {
    for (auto it = manifest.files.cbegin(); it != manifest.files.cend(); ++it) {
        const QString path = QDir(root).filePath(it.key());
        const QFileInfo info(path);
        if (!safeDestination(root, it.key()) || !info.isFile() || info.size() != it->size
            || hashFile(path) != it->sha256)
            return fail(error, appUpdateText("文件已修改、缺失或校验失败：%1").arg(it.key()));
    }
    return true;
}

bool AppUpdateManifest::preflight(const QString& target, const QString& stage, QString* error) {
    AppUpdateManifest oldManifest, newManifest;
    if (!read(target, &oldManifest, error) || !read(stage, &newManifest, error)
        || !verify(target, oldManifest, error) || !verify(stage, newManifest, error)) return false;
    SemanticVersion oldVersion, newVersion;
    if (!SemanticVersion::parse(oldManifest.version, &oldVersion)
        || !SemanticVersion::parse(newManifest.version, &newVersion)
        || newVersion.compare(oldVersion) <= 0)
        return fail(error, appUpdateText("更新版本必须高于当前版本"));
    QSet<QString> oldNames;
    for (auto it = oldManifest.files.cbegin(); it != oldManifest.files.cend(); ++it)
        oldNames.insert(it.key().toCaseFolded());
    for (auto it = newManifest.files.cbegin(); it != newManifest.files.cend(); ++it) {
        if (!safeDestination(target, it.key()))
            return fail(error, appUpdateText("更新目标包含链接：%1").arg(it.key()));
        const QString path = QDir(target).filePath(it.key());
        if (!oldNames.contains(it.key().toCaseFolded()) && QFileInfo::exists(path))
            return fail(error, appUpdateText("新增文件与本地文件冲突：%1").arg(it.key()));
        QString parent = QFileInfo(path).absolutePath();
        while (parent.size() > target.size()) {
            if (QFileInfo::exists(parent) && !QFileInfo(parent).isDir())
                return fail(error, appUpdateText("更新路径被本地文件占用：%1").arg(it.key()));
            parent = QFileInfo(parent).absolutePath();
        }
    }
    return true;
}

bool writeAppUpdateJson(const QString& path, const QJsonObject& object, QString* error) {
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, appUpdateText("无法写入更新事务：%1").arg(file.errorString()));
    return true;
}

QJsonObject readAppUpdateJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
