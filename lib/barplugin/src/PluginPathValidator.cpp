// PluginPathValidator.cpp — 解析 Windows junction 后确认文件仍位于插件目录
#include "barplugin/PluginPathValidator.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString finalCanonicalPath(const QString& path) {
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    const DWORD required = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    QString resolved;
    if (required > 0) {
        resolved.resize(static_cast<qsizetype>(required));
        const DWORD written = GetFinalPathNameByHandleW(
            handle, reinterpret_cast<LPWSTR>(resolved.data()), required,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (written > 0 && written < required)
            resolved.resize(static_cast<qsizetype>(written));
        else
            resolved.clear();
    }
    CloseHandle(handle);
    if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        resolved = QStringLiteral("\\\\") + resolved.mid(8);
    else if (resolved.startsWith(QStringLiteral("\\\\?\\")))
        resolved.remove(0, 4);
    return QDir::cleanPath(QDir::fromNativeSeparators(resolved));
#else
    return QDir::cleanPath(QFileInfo(path).canonicalFilePath());
#endif
}

void setError(QString* error, const QString& text) {
    if (error) *error = text;
}
} // namespace

bool xuanhub::validatePluginFilePath(const QString& pluginDir, const QString& field,
                                     const QString& relativePath, QString* error) {
    if (relativePath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("manifest path '%1' must not be empty").arg(field));
        return false;
    }
    if (QDir::isAbsolutePath(relativePath)
        || QRegularExpression(QStringLiteral(R"(^[A-Za-z]:[\\/])"))
               .match(relativePath).hasMatch()) {
        setError(error, QStringLiteral("manifest path '%1' must be relative: %2")
                            .arg(field, relativePath));
        return false;
    }

    const QString cleanRelative = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    if (cleanRelative == QLatin1String("..")
        || cleanRelative.startsWith(QStringLiteral("../"))) {
        setError(error, QStringLiteral("manifest path '%1' escapes plugin directory: %2")
                            .arg(field, relativePath));
        return false;
    }

    const QString rootCanonical = finalCanonicalPath(pluginDir);
    if (rootCanonical.isEmpty()) {
        setError(error, QStringLiteral("plugin directory does not exist: %1").arg(pluginDir));
        return false;
    }
    const QFileInfo targetInfo(QDir(pluginDir).absoluteFilePath(cleanRelative));
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        setError(error, QStringLiteral(
            "manifest path '%1' does not name an existing file: %2")
                            .arg(field, relativePath));
        return false;
    }
    const QString targetCanonical = finalCanonicalPath(targetInfo.absoluteFilePath());
    const QString rootPrefix = rootCanonical + QLatin1Char('/');
    if (!targetCanonical.startsWith(rootPrefix, Qt::CaseInsensitive)) {
        setError(error, QStringLiteral(
            "manifest path '%1' resolves outside plugin directory: %2")
                            .arg(field, relativePath));
        return false;
    }
    if (error) error->clear();
    return true;
}
