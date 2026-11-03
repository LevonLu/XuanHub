// PluginCatalog.cpp — 扫描目录、受管版本覆盖、清单与 IID 校验
#include "PluginCatalog.h"
#include "barplugin/ManagedPluginStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibrary>
#include <QDebug>
namespace {
QStringList nativeBackendCandidates(const QDir& directory) {
    QFile manifest(directory.filePath(QStringLiteral("metadata.json")));
    if (manifest.open(QIODevice::ReadOnly) && manifest.size() <= 1024 * 1024) {
        const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
        const QString backend = document.isObject()
            ? document.object().value(QStringLiteral("entry"))
                  .toObject().value(QStringLiteral("backend")).toString().trimmed()
            : QString();
        if (!backend.isEmpty()) return {directory.absoluteFilePath(backend)};
    }
    QStringList result;
    for (const QString& dll : directory.entryList(
             {QStringLiteral("*.dll")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        result.append(directory.absoluteFilePath(dll));
    }
    return result;
}
} // namespace

PluginCatalog::Result PluginCatalog::scan(
    const QString& pluginsDir, const QString& configDir,
    const QSet<QString>& enabledIds, const QSet<QString>& loadedIds,
    const QHash<QString, QString>& unloadErrors) {
    Result result;
    struct ScanDirectory {
        QString idHint;
        QString path;
        bool managed = false;
    };
    QVector<ScanDirectory> scanDirectories;
    QSet<QString> managedIds;
    for (const auto& managed : xuanhub::readManagedPluginCurrents(
             configDir, QStringLiteral("native"))) {
        scanDirectories.append({managed.id, managed.payloadDirectory, true});
        managedIds.insert(managed.id);
    }
    QDir base(pluginsDir);
    if (base.exists()) {
        for (const QString& sub : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            scanDirectories.append({sub, base.filePath(sub), false});
    } else {
        qDebug() << "[Plugin] built-in plugins dir not found:" << pluginsDir;
    }
    if (scanDirectories.isEmpty()) {
        for (const auto& id : enabledIds)
            result.errors.insert(id, QStringLiteral("no plugin directories are available"));
        return result;
    }
    const PluginValidationEnvironment validationEnvironment =
        PluginValidationEnvironment::current();
    QSet<QString> discoveredIds = loadedIds;
    for (const ScanDirectory& scan : scanDirectories) {
        QDir d(scan.path);
        const QStringList backendCandidates = nativeBackendCandidates(d);
        for (const QString& path : backendCandidates) {
            const QString dllName = QFileInfo(path).fileName();
            // sidecar 是发现阶段的信任边界：必须在构造 QPluginLoader 前完成
            // 解析和路径/ABI 校验，失败插件的 DLL 代码完全不进入宿主进程。
            PluginManifestResult manifest = PluginManifest::load(
                d.absolutePath(), dllName, validationEnvironment);
            if (!manifest.ok) {
                qWarning() << "[Plugin] manifest rejected:" << path << "->"
                           << manifest.error;
                if (enabledIds.contains(scan.idHint)) result.errors.insert(scan.idHint, manifest.error);
                continue;
            }
            const QString manifestId = manifest.descriptor.id;
            if (!enabledIds.contains(manifestId)) continue;
            if (unloadErrors.contains(manifestId)) {
                result.errors.insert(manifestId, unloadErrors.value(manifestId));
                continue;
            }
            if (!scan.managed && managedIds.contains(manifestId)) {
                qDebug() << "[Plugin] built-in plugin shadowed by managed version:"
                         << manifestId << "->" << path;
                continue;
            }
            if (discoveredIds.contains(manifestId)) {
                qWarning() << "[Plugin] duplicate manifest id, skip:"
                           << manifestId << "->" << path;
                continue;
            }
            discoveredIds.insert(manifestId);
            // metaData() 读取 Qt 插件节，不调用 instance()，可安全参与预加载依赖规划。
            auto probe = std::make_unique<QPluginLoader>();
            // probe 与正式 loader 共享 Qt 的库缓存。QObject 已销毁时，Qt 仍可能
            // 缓存此 DLL 的元类型/元对象；物理卸载会让排队参数和 QML 缓存悬空。
            // 保留 Qt 的驻留保护，unload 仍销毁根实例；同路径 DLL 替换须退出宿主。
            probe->setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
            probe->setFileName(path);
            const QString iid = probe->metaData().value(QStringLiteral("IID")).toString();
            if (iid != QStringLiteral("xuanhub.BarPlugin/1.0")
                && iid != QStringLiteral("xuanhub.BarPlugin/2.0")) {
                qWarning() << "[Plugin] skip (IID mismatch):" << path << "->" << iid;
                result.errors.insert(manifestId,
                    QStringLiteral("unsupported plugin IID: %1").arg(iid));
                continue;
            }
            const int iidApiVersion = iid.endsWith(QStringLiteral("/2.0")) ? 2 : 1;
            if (manifest.descriptor.apiVersion != iidApiVersion) {
                qWarning() << "[Plugin] manifest apiVersion/IID mismatch:" << path
                           << manifest.descriptor.apiVersion << iid;
                result.errors.insert(manifestId,
                    QStringLiteral("manifest apiVersion does not match plugin IID"));
                continue;
            }
            result.candidates.push_back({d.absolutePath(), path, iid,
                                         std::move(manifest), std::move(probe)});
        }
    }

    return result;
}
