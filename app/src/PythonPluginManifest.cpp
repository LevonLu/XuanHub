// PythonPluginManifest.cpp — Python Manifest 字段、宿主范围和目录边界校验实现
#include "PythonPluginManifest.h"

#include "SemanticVersion.h"
#include "barplugin/PluginPathValidator.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {
PluginManifestResult failed(const QString& error) {
    PluginManifestResult result;
    result.error = error;
    return result;
}

bool requiredString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        *error = QStringLiteral("manifest field '%1' must be a non-empty string")
                     .arg(key);
        return false;
    }
    *output = value.toString().trimmed();
    return true;
}

bool optionalString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) return true;
    if (!value.isString()) {
        *error = QStringLiteral("manifest field '%1' must be a string").arg(key);
        return false;
    }
    *output = value.toString().trimmed();
    return true;
}

bool stringArray(const QJsonObject& object, const QString& key,
                 QStringList* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (value.isUndefined()) return true;
    if (!value.isArray()) {
        *error = QStringLiteral("manifest field '%1' must be an array").arg(key);
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue& item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!item.isString() || text.isEmpty() || seen.contains(text)) {
            *error = QStringLiteral(
                "manifest field '%1' contains an invalid or duplicate value").arg(key);
            return false;
        }
        seen.insert(text);
        output->append(text);
    }
    return true;
}
} // namespace

PluginManifestResult PythonPluginManifest::load(
    const QString& pluginDir, const PluginValidationEnvironment& environment) {
    QFile file(QDir(pluginDir).filePath(QStringLiteral("plugin.json")));
    if (!file.open(QIODevice::ReadOnly))
        return failed(QStringLiteral("cannot open Python plugin manifest"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failed(QStringLiteral("invalid Python plugin manifest JSON: %1")
                          .arg(parseError.errorString()));
    }

    const QJsonObject object = document.object();
    xuanhub::PluginDescriptor descriptor;
    QString error;
    if (!requiredString(object, QStringLiteral("id"), &descriptor.id, &error)
        || !requiredString(object, QStringLiteral("name"), &descriptor.displayName, &error)
        || !requiredString(object, QStringLiteral("version"), &descriptor.version, &error)) {
        return failed(error);
    }
    static const QRegularExpression idPattern(
        QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
    if (!idPattern.match(descriptor.id).hasMatch())
        return failed(QStringLiteral("Python plugin id is invalid"));
    if (!SemanticVersion::parse(descriptor.version, nullptr, &error))
        return failed(QStringLiteral("Python plugin version is invalid: %1").arg(error));

    const QJsonValue apiVersion = object.value(QStringLiteral("apiVersion"));
    if (!apiVersion.isUndefined()) {
        if (!apiVersion.isDouble() || apiVersion.toDouble() != apiVersion.toInt()
            || (apiVersion.toInt() != 1 && apiVersion.toInt() != 2)) {
            return failed(QStringLiteral("unsupported Python plugin apiVersion"));
        }
        descriptor.apiVersion = apiVersion.toInt();
    }
    const QJsonValue configVersion = object.value(QStringLiteral("configVersion"));
    if (!configVersion.isUndefined()) {
        if (!configVersion.isDouble() || configVersion.toDouble() != configVersion.toInt()
            || configVersion.toInt() < 1) {
            return failed(QStringLiteral("Python configVersion must be positive"));
        }
        descriptor.configVersion = configVersion.toInt();
    }
    if (!optionalString(object, QStringLiteral("author"), &descriptor.author, &error)
        || !optionalString(object, QStringLiteral("minHostVersion"),
                           &descriptor.minHostVersion, &error)
        || !optionalString(object, QStringLiteral("maxHostVersion"),
                           &descriptor.maxHostVersion, &error)
        || !optionalString(object, QStringLiteral("qml"), &descriptor.entry.qml, &error)
        || !optionalString(object, QStringLiteral("settingsQml"),
                           &descriptor.entry.settingsQml, &error)) {
        return failed(error);
    }
    if (!PluginManifest::validateHostCompatibility(
            descriptor, environment.hostVersion, &error)
        || !stringArray(object, QStringLiteral("capabilities"),
                        &descriptor.capabilities, &error)
        || !stringArray(object, QStringLiteral("permissions"),
                        &descriptor.permissions, &error)) {
        return failed(error);
    }

    descriptor.entry.backend = QStringLiteral("main.py");
    if (!xuanhub::validatePluginFilePath(pluginDir, QStringLiteral("backend"),
                                         descriptor.entry.backend, &error)
        || (!descriptor.entry.qml.isEmpty()
            && !xuanhub::validatePluginFilePath(
                pluginDir, QStringLiteral("qml"), descriptor.entry.qml, &error))
        || (!descriptor.entry.settingsQml.isEmpty()
            && !xuanhub::validatePluginFilePath(
                pluginDir, QStringLiteral("settingsQml"),
                descriptor.entry.settingsQml, &error))) {
        return failed(error);
    }

    PluginManifestResult result;
    result.ok = true;
    result.descriptor = descriptor;
    return result;
}
