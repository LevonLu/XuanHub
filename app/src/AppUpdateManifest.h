// AppUpdateManifest.h — 校验宿主发行清单、受保护路径和更新前的文件冲突
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

struct AppUpdateFile {
    qint64 size = 0;
    QString sha256;
};

struct AppUpdateManifest {
    QString version;
    QMap<QString, AppUpdateFile> files;

    static constexpr qint64 MaximumFileBytes = 256LL * 1024 * 1024;
    static constexpr qint64 MaximumTotalBytes = 2LL * 1024 * 1024 * 1024;
    static constexpr qint64 MaximumPackageBytes = 1024LL * 1024 * 1024;
    static QString fileName();
    static bool safeRelativePath(const QString& path);
    static bool protectedPath(const QString& path);
    static bool safeDestination(const QString& root, const QString& relative);
    static QString hashFile(const QString& path);
    static bool parse(const QByteArray& bytes, AppUpdateManifest* result, QString* error);
    static bool read(const QString& root, AppUpdateManifest* result, QString* error);
    static bool verify(const QString& root, const AppUpdateManifest& manifest, QString* error);
    static bool preflight(const QString& target, const QString& stage, QString* error);
};

// 事务文件采用原子写入，避免更新进程意外中止留下截断 JSON。
bool writeAppUpdateJson(const QString& path, const QJsonObject& object, QString* error);
QJsonObject readAppUpdateJson(const QString& path);
QString appUpdateText(const char* source);
