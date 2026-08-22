// PluginPackageStore.cpp — 以不可变版本目录和原子指针实现可恢复的插件安装事务
#include "PluginPackageStore.h"

#include "SemanticVersion.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
const QRegularExpression kPluginIdPattern(
    QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));

PluginPackageStoreResult failed(const QString& error,
                                const QString& id = {},
                                const QString& version = {}) {
    PluginPackageStoreResult result;
    result.error = error;
    result.id = id;
    result.version = version;
    return result;
}

QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject{};
}

bool writeObject(const QString& path, const QJsonObject& object, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QStringLiteral("cannot create record directory");
        return false;
    }
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("cannot atomically write record: %1").arg(path);
        return false;
    }
    return true;
}

bool validIdentity(const QString& id, const QString& version) {
    return kPluginIdPattern.match(id).hasMatch()
        && SemanticVersion::parse(version, nullptr);
}

bool isWithin(const QString& root, const QString& target) {
    const QString rootPath = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString targetPath = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
    return targetPath.startsWith(rootPath + QLatin1Char('/'),
                                 Qt::CaseInsensitive);
}

bool hasLinkOrJunction(const QString& path) {
    const QFileInfo rootInfo(path);
    if (rootInfo.isSymLink() || rootInfo.isJunction()) return true;
    QDirIterator iterator(path, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink() || info.isJunction()) return true;
    }
    return false;
}

bool removeTreeWithin(const QString& root, const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) return true;
    if (!isWithin(root, path) || hasLinkOrJunction(path)) return false;
    return QDir(path).removeRecursively();
}

bool callHook(const std::function<bool(const QString&, QString*)>& hook,
              const QString& id, const QString& action, QString* error) {
    if (!hook) {
        *error = QStringLiteral("activation hook '%1' is unavailable").arg(action);
        return false;
    }
    QString hookError;
    if (!hook(id, &hookError)) {
        *error = hookError.isEmpty()
            ? QStringLiteral("activation hook '%1' failed").arg(action)
            : hookError;
        return false;
    }
    return true;
}

bool injected(const PluginPackageStore::FaultInjector& injector,
              PluginPackageStore::FaultPoint point) {
    return injector && injector(point);
}
} // namespace

PluginPackageStore::PluginPackageStore(QString configDirectory)
    : m_configDirectory(QDir::cleanPath(QFileInfo(configDirectory).absoluteFilePath())),
      m_storeRoot(storeRootForConfigDirectory(m_configDirectory)),
      m_dataRoot(QDir(m_configDirectory).filePath(QStringLiteral("plugin-data"))) {}

QString PluginPackageStore::storeRootForConfigDirectory(
    const QString& configDirectory) {
    return QDir(QFileInfo(configDirectory).absoluteFilePath())
        .filePath(QStringLiteral("plugin-store"));
}

QString PluginPackageStore::storeRoot() const { return m_storeRoot; }
QString PluginPackageStore::dataRoot() const { return m_dataRoot; }

QString PluginPackageStore::pluginDirectory(const QString& id) const {
    return QDir(m_storeRoot).filePath(id);
}

QString PluginPackageStore::versionDirectory(
    const QString& id, const QString& version) const {
    return QDir(pluginDirectory(id)).filePath(
        QStringLiteral("versions/") + version);
}

QString PluginPackageStore::currentFile(const QString& id) const {
    return QDir(pluginDirectory(id)).filePath(QStringLiteral("current.json"));
}

bool PluginPackageStore::writeCurrent(
    const QString& id, const QString& version,
    const QString& previousVersion, QString* error) const {
    QJsonObject object{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("version"), version},
    };
    if (!previousVersion.isEmpty())
        object.insert(QStringLiteral("previousVersion"), previousVersion);
    return writeObject(currentFile(id), object, error);
}

bool PluginPackageStore::removeCurrent(const QString& id, QString* error) const {
    const QString path = currentFile(id);
    if (!QFileInfo::exists(path) || QFile::remove(path)) return true;
    *error = QStringLiteral("cannot remove current version pointer");
    return false;
}

bool PluginPackageStore::copyPayload(
    const PluginPackageValidationResult& package,
    const QString& destination,
    QString* error) const {
    const QString payloadDestination =
        QDir(destination).filePath(QStringLiteral("payload"));
    if (!QDir().mkpath(payloadDestination)) {
        *error = QStringLiteral("cannot create version staging directory");
        return false;
    }
    int copied = 0;
    for (const PluginPackageFile& entry : package.files) {
        if (!entry.path.startsWith(QStringLiteral("payload/"))) continue;
        const QString relative = entry.path.mid(8);
        if (relative.isEmpty()) continue;
        const QString source = QDir(package.payloadDirectory).filePath(relative);
        const QString target = QDir(payloadDestination).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            *error = QStringLiteral("cannot create staging parent for %1").arg(relative);
            return false;
        }
        QFile input(source);
        QSaveFile output(target);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("cannot stage payload file: %1").arg(relative);
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!input.atEnd()) {
            const QByteArray chunk = input.read(1024 * 1024);
            if ((chunk.isEmpty() && input.error() != QFile::NoError)
                || output.write(chunk) != chunk.size()) {
                *error = QStringLiteral("cannot copy payload file: %1").arg(relative);
                return false;
            }
            hash.addData(chunk);
        }
        if (QString::fromLatin1(hash.result().toHex()) != entry.sha256) {
            *error = QStringLiteral("staged payload hash changed: %1").arg(relative);
            return false;
        }
        if (!output.commit()) {
            *error = QStringLiteral("cannot commit staged payload file: %1").arg(relative);
            return false;
        }
        ++copied;
    }
    if (copied == 0) {
        *error = QStringLiteral("package payload contains no files");
        return false;
    }
    return true;
}

void PluginPackageStore::cleanupTransientDirectories() const {
    QDir root(m_storeRoot);
    if (!root.exists()) return;
    const QStringList transients = root.entryList(
        {QStringLiteral(".trash-*"), QStringLiteral(".staging-*")},
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& transient : transients)
        removeTreeWithin(m_storeRoot, root.filePath(transient));
    for (const QString& id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir versions(QDir(root.filePath(id)).filePath(QStringLiteral("versions")));
        if (!versions.exists()) continue;
        for (const QString& transient : versions.entryList(
                 {QStringLiteral(".staging-*")},
                 QDir::Dirs | QDir::NoDotAndDotDot)) {
            removeTreeWithin(m_storeRoot, versions.filePath(transient));
        }
    }
}

ManagedPluginVersion PluginPackageStore::current(const QString& id) const {
    ManagedPluginVersion result;
    if (!kPluginIdPattern.match(id).hasMatch()) return result;
    const QJsonObject pointer = readObject(currentFile(id));
    const QString version = pointer.value(QStringLiteral("version")).toString();
    const QString previous = pointer.value(QStringLiteral("previousVersion")).toString();
    if (!validIdentity(id, version)
        || (!previous.isEmpty() && !SemanticVersion::parse(previous, nullptr))) {
        return {};
    }
    const QString directory = versionDirectory(id, version);
    const QJsonObject record = readObject(
        QDir(directory).filePath(QStringLiteral("install.json")));
    const QString runtime = record.value(QStringLiteral("runtime")).toString();
    const QString payload = QDir(directory).filePath(QStringLiteral("payload"));
    if ((runtime != QLatin1String("native") && runtime != QLatin1String("python"))
        || !QFileInfo(payload).isDir()) {
        return {};
    }
    result.id = id;
    result.version = version;
    result.previousVersion = previous;
    result.runtime = runtime;
    result.payloadDirectory = payload;
    result.installRecord = record;
    return result;
}

QList<ManagedPluginVersion> PluginPackageStore::currentVersions(
    const QString& runtime) const {
    QList<ManagedPluginVersion> result;
    QDir root(m_storeRoot);
    if (!root.exists()) return result;
    const QStringList ids = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                           QDir::Name | QDir::IgnoreCase);
    for (const QString& id : ids) {
        if (!kPluginIdPattern.match(id).hasMatch()) continue;
        ManagedPluginVersion version = current(id);
        if (version.id.isEmpty() || (!runtime.isEmpty() && version.runtime != runtime))
            continue;
        result.append(std::move(version));
    }
    return result;
}

PluginPackageStoreResult PluginPackageStore::install(
    const QString& packagePath,
    const ActivationHooks& hooks,
    const FaultInjector& faultInjector,
    const PluginValidationEnvironment& environment) {
    cleanupTransientDirectories();
    PluginPackageValidationResult package =
        PluginPackageValidator::validateAndExtract(packagePath, environment);
    if (!package.ok) return failed(package.error);
    const QString id = package.descriptor.id;
    const QString version = package.descriptor.version;
    const ManagedPluginVersion oldCurrent = current(id);
    const QString previousVersion = oldCurrent.version;
    const bool active = hooks.isActive && hooks.isActive(id);
    if (active && (!hooks.stop || !hooks.start))
        return failed(QStringLiteral("active plugin requires stop and start hooks"), id, version);

    if (!QDir().mkpath(QDir(pluginDirectory(id)).filePath(QStringLiteral("versions"))))
        return failed(QStringLiteral("cannot create plugin version store"), id, version);
    const QString finalDirectory = versionDirectory(id, version);
    bool createdVersion = false;
    if (QFileInfo::exists(finalDirectory)) {
        const QJsonObject existing = readObject(
            QDir(finalDirectory).filePath(QStringLiteral("install.json")));
        if (existing.value(QStringLiteral("packageHash")).toString()
            != package.packageHash) {
            return failed(QStringLiteral("immutable version already exists with another hash"),
                          id, version);
        }
    } else {
        const QString stagingDirectory = QDir(pluginDirectory(id)).filePath(
            QStringLiteral("versions/.staging-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!copyPayload(package, stagingDirectory, &package.error)) {
            removeTreeWithin(m_storeRoot, stagingDirectory);
            return failed(package.error, id, version);
        }
        QJsonObject record{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("id"), id},
            {QStringLiteral("version"), version},
            {QStringLiteral("runtime"), package.runtime},
            {QStringLiteral("source"), QStringLiteral("local-file")},
            {QStringLiteral("sourceName"), QFileInfo(packagePath).fileName()},
            {QStringLiteral("installedAt"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("packageHash"), package.packageHash},
            {QStringLiteral("publisher"), package.publisher},
        };
        if (!previousVersion.isEmpty())
            record.insert(QStringLiteral("previousVersion"), previousVersion);
        if (!writeObject(QDir(stagingDirectory).filePath(QStringLiteral("install.json")),
                         record, &package.error)
            || !QDir().rename(stagingDirectory, finalDirectory)) {
            removeTreeWithin(m_storeRoot, stagingDirectory);
            return failed(package.error.isEmpty()
                    ? QStringLiteral("cannot commit immutable plugin version")
                    : package.error,
                id, version);
        }
        createdVersion = true;
    }

    auto removeCreatedVersion = [&]() {
        if (createdVersion) removeTreeWithin(m_storeRoot, finalDirectory);
    };
    if (injected(faultInjector, FaultPoint::AfterStaging)) {
        removeCreatedVersion();
        return failed(QStringLiteral("fault injected after staging"), id, version);
    }

    QString activationError;
    if (active && !callHook(hooks.stop, id, QStringLiteral("stop"), &activationError)) {
        removeCreatedVersion();
        return failed(activationError, id, version);
    }
    if (injected(faultInjector, FaultPoint::AfterStop)) {
        if (active) callHook(hooks.start, id, QStringLiteral("restart"), &activationError);
        removeCreatedVersion();
        return failed(QStringLiteral("fault injected after stop"), id, version);
    }

    if (!writeCurrent(id, version,
                      previousVersion == version ? QString() : previousVersion,
                      &activationError)) {
        if (active) callHook(hooks.start, id, QStringLiteral("restart"), &activationError);
        removeCreatedVersion();
        return failed(activationError, id, version);
    }

    const bool switchFault = injected(faultInjector, FaultPoint::AfterSwitch);
    const bool startFailed = active && !switchFault
        && !callHook(hooks.start, id, QStringLiteral("start"), &activationError);
    if (switchFault || startFailed) {
        QString rollbackError;
        const bool pointerRestored = previousVersion.isEmpty()
            ? removeCurrent(id, &rollbackError)
            : writeCurrent(id, previousVersion, {}, &rollbackError);
        bool runtimeRestored = true;
        if (active && pointerRestored)
            runtimeRestored = callHook(hooks.start, id, QStringLiteral("rollback"),
                                       &rollbackError);
        removeCreatedVersion();
        PluginPackageStoreResult result = failed(
            switchFault ? QStringLiteral("fault injected after switch") : activationError,
            id, version);
        result.previousVersion = previousVersion;
        result.rolledBack = pointerRestored && runtimeRestored;
        if (!result.rolledBack && !rollbackError.isEmpty())
            result.error += QStringLiteral("; rollback failed: ") + rollbackError;
        return result;
    }

    PluginPackageStoreResult result;
    result.ok = true;
    result.id = id;
    result.version = version;
    result.previousVersion = previousVersion;
    result.payloadDirectory = QDir(finalDirectory).filePath(QStringLiteral("payload"));
    return result;
}

PluginPackageStoreResult PluginPackageStore::rollback(
    const QString& id,
    const ActivationHooks& hooks) {
    cleanupTransientDirectories();
    const ManagedPluginVersion oldCurrent = current(id);
    if (oldCurrent.id.isEmpty())
        return failed(QStringLiteral("plugin is not managed or has no current version"), id);
    if (oldCurrent.previousVersion.isEmpty())
        return failed(QStringLiteral("plugin has no rollback version"), id, oldCurrent.version);
    const QString targetDirectory = versionDirectory(id, oldCurrent.previousVersion);
    if (!QFileInfo(QDir(targetDirectory).filePath(QStringLiteral("payload"))).isDir())
        return failed(QStringLiteral("rollback version payload is missing"),
                      id, oldCurrent.previousVersion);
    const bool active = hooks.isActive && hooks.isActive(id);
    QString error;
    if (active && (!callHook(hooks.stop, id, QStringLiteral("stop"), &error)))
        return failed(error, id, oldCurrent.previousVersion);
    if (!writeCurrent(id, oldCurrent.previousVersion, oldCurrent.version, &error)) {
        if (active) callHook(hooks.start, id, QStringLiteral("restart"), &error);
        return failed(error, id, oldCurrent.previousVersion);
    }
    if (active && !callHook(hooks.start, id, QStringLiteral("start"), &error)) {
        QString rollbackError;
        const bool restored = writeCurrent(
            id, oldCurrent.version, oldCurrent.previousVersion, &rollbackError)
            && callHook(hooks.start, id, QStringLiteral("rollback"), &rollbackError);
        PluginPackageStoreResult result = failed(error, id, oldCurrent.previousVersion);
        result.rolledBack = restored;
        if (!restored && !rollbackError.isEmpty())
            result.error += QStringLiteral("; rollback failed: ") + rollbackError;
        return result;
    }
    PluginPackageStoreResult result;
    result.ok = true;
    result.id = id;
    result.version = oldCurrent.previousVersion;
    result.previousVersion = oldCurrent.version;
    result.payloadDirectory = QDir(targetDirectory).filePath(QStringLiteral("payload"));
    return result;
}

PluginPackageStoreResult PluginPackageStore::uninstall(
    const QString& id,
    bool deleteUserData,
    const ActivationHooks& hooks) {
    cleanupTransientDirectories();
    const ManagedPluginVersion oldCurrent = current(id);
    if (oldCurrent.id.isEmpty())
        return failed(QStringLiteral("plugin is not managed or has no current version"), id);
    const bool active = hooks.isActive && hooks.isActive(id);
    QString error;
    if (active && !callHook(hooks.stop, id, QStringLiteral("stop"), &error))
        return failed(error, id, oldCurrent.version);

    const QString sourceDirectory = pluginDirectory(id);
    const QString trashDirectory = QDir(m_storeRoot).filePath(
        QStringLiteral(".trash-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().rename(sourceDirectory, trashDirectory)) {
        if (active) callHook(hooks.start, id, QStringLiteral("restart"), &error);
        return failed(QStringLiteral("cannot detach plugin from managed store"),
                      id, oldCurrent.version);
    }

    PluginPackageStoreResult result;
    result.ok = true;
    result.id = id;
    result.version = oldCurrent.version;
    if (!removeTreeWithin(m_storeRoot, trashDirectory)) result.cleanupPending = true;
    if (deleteUserData) {
        const QString userData = QDir(m_dataRoot).filePath(id);
        if (!removeTreeWithin(m_dataRoot, userData)) result.cleanupPending = true;
    }
    return result;
}
