// PluginPackageValidator.h — 在执行安装前离线校验并安全展开 .xuanplugin 包
#pragma once

#include "PluginManifest.h"

#include <QJsonObject>
#include <QSharedPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

struct PluginPackageFile {
    QString path;
    qint64 compressedSize = 0;
    qint64 uncompressedSize = 0;
    QString sha256;
};

struct PluginPackageValidationResult {
    bool ok = false;
    QString error;
    QString runtime;
    QString publisher;
    QString packageHash;
    QString signatureStatus = QStringLiteral("unsigned");
    QString signatureAlgorithm;
    QString signerKeyId;
    QString payloadDirectory;
    QStringList declaredPermissions;
    QStringList grantedPermissions;
    QStringList unsupportedPermissions;
    xuanhub::PluginDescriptor descriptor;
    QVector<PluginPackageFile> files;

    // 校验结果拥有临时目录；结果销毁后自动清理，安装事务必须在其生命周期内完成。
    QSharedPointer<QTemporaryDir> temporaryDirectory;
};

class PluginPackageValidator final {
public:
    static constexpr int MaximumEntries = 2048;
    static constexpr qint64 MaximumArchiveBytes = 256LL * 1024 * 1024;
    static constexpr qint64 MaximumFileBytes = 64LL * 1024 * 1024;
    static constexpr qint64 MaximumExpandedBytes = 512LL * 1024 * 1024;
    static constexpr int MaximumCompressionRatio = 200;

    static PluginPackageValidationResult validateAndExtract(
        const QString& packagePath,
        const PluginValidationEnvironment& environment =
            PluginValidationEnvironment::current());
};
