// ManagedPluginStore.cpp — 严格解析原子 current.json，不执行或加载任何插件代码
#include "barplugin/ManagedPluginStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace {
const QRegularExpression kPluginIdPattern(
    QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
const QRegularExpression kVersionPathPattern(
    QStringLiteral(R"(^[0-9A-Za-z][0-9A-Za-z.+-]{0,127}$)"));

QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject{};
}

bool isWithin(const QString& root, const QString& path) {
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    return !canonicalRoot.isEmpty() && !canonicalPath.isEmpty()
        && canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'),
                                    Qt::CaseInsensitive);
}
} // namespace

QString xuanhub::managedPluginStoreRoot(const QString& configDirectory) {
    return QDir(QFileInfo(configDirectory).absoluteFilePath())
        .filePath(QStringLiteral("plugin-store"));
}

xuanhub::ManagedPluginVersion xuanhub::readManagedPluginCurrent(
    const QString& configDirectory,
    const QString& id) {
    ManagedPluginVersion result;
    if (!kPluginIdPattern.match(id).hasMatch()) return result;
    const QString storeRoot = managedPluginStoreRoot(configDirectory);
    const QString pluginDirectory = QDir(storeRoot).filePath(id);
    const QJsonObject pointer = readObject(
        QDir(pluginDirectory).filePath(QStringLiteral("current.json")));
    if (pointer.value(QStringLiteral("schemaVersion")).toInt() != 1)
        return result;
    const QString version = pointer.value(QStringLiteral("version")).toString();
    const QString previous = pointer.value(QStringLiteral("previousVersion")).toString();
    if (!kVersionPathPattern.match(version).hasMatch()
        || (!previous.isEmpty() && !kVersionPathPattern.match(previous).hasMatch())) {
        return result;
    }
    const QString versionDirectory = QDir(pluginDirectory).filePath(
        QStringLiteral("versions/") + version);
    const QJsonObject record = readObject(
        QDir(versionDirectory).filePath(QStringLiteral("install.json")));
    const QString runtime = record.value(QStringLiteral("runtime")).toString();
    const QString payload = QDir(versionDirectory).filePath(QStringLiteral("payload"));
    const QFileInfo payloadInfo(payload);
    if (record.value(QStringLiteral("schemaVersion")).toInt() != 1
        || record.value(QStringLiteral("id")).toString() != id
        || record.value(QStringLiteral("version")).toString() != version
        || (runtime != QLatin1String("native") && runtime != QLatin1String("python"))
        || !payloadInfo.isDir() || payloadInfo.isSymLink() || payloadInfo.isJunction()
        || !isWithin(storeRoot, payload)) {
        return {};
    }
    result.id = id;
    result.version = version;
    result.previousVersion = previous;
    result.runtime = runtime;
    result.payloadDirectory = payloadInfo.canonicalFilePath();
    result.installRecord = record;
    return result;
}

QList<xuanhub::ManagedPluginVersion> xuanhub::readManagedPluginCurrents(
    const QString& configDirectory,
    const QString& runtime) {
    QList<ManagedPluginVersion> result;
    QDir root(managedPluginStoreRoot(configDirectory));
    if (!root.exists()) return result;
    const QStringList ids = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                           QDir::Name | QDir::IgnoreCase);
    for (const QString& id : ids) {
        ManagedPluginVersion version = readManagedPluginCurrent(configDirectory, id);
        if (version.id.isEmpty() || (!runtime.isEmpty() && version.runtime != runtime))
            continue;
        result.append(std::move(version));
    }
    return result;
}
