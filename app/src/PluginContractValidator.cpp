// PluginContractValidator.cpp — 宿主与开发工具共用的插件静态契约校验实现
#include "PluginContractValidator.h"

#include "PythonPluginManifest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
const QSet<QString> kFieldTypes{
    QStringLiteral("bool"), QStringLiteral("int"), QStringLiteral("double"),
    QStringLiteral("string"), QStringLiteral("color"), QStringLiteral("enum"),
    QStringLiteral("json"), QStringLiteral("object"), QStringLiteral("array")};

QJsonObject readObject(const QString& path, QStringList* errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors->append(QStringLiteral("cannot open JSON file: %1").arg(path));
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        errors->append(QStringLiteral("invalid JSON file %1: %2")
                           .arg(path, parseError.errorString()));
        return {};
    }
    return document.object();
}

void validateSettings(const QJsonValue& value, QStringList* errors) {
    if (value.isUndefined()) return;
    if (!value.isArray()) {
        errors->append(QStringLiteral("settings must be an array"));
        return;
    }
    QSet<QString> keys;
    const QJsonArray groups = value.toArray();
    for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        if (!groups.at(groupIndex).isObject()
            || !groups.at(groupIndex).toObject().value(QStringLiteral("fields")).isArray()) {
            errors->append(QStringLiteral("settings[%1] must contain a fields array")
                               .arg(groupIndex));
            continue;
        }
        const QJsonArray fields = groups.at(groupIndex).toObject()
                                      .value(QStringLiteral("fields")).toArray();
        for (qsizetype fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
            const QString location = QStringLiteral("settings[%1].fields[%2]")
                                         .arg(groupIndex).arg(fieldIndex);
            if (!fields.at(fieldIndex).isObject()) {
                errors->append(location + QStringLiteral(" must be an object"));
                continue;
            }
            const QJsonObject field = fields.at(fieldIndex).toObject();
            const QString key = field.value(QStringLiteral("key")).toString();
            const QString type = field.value(QStringLiteral("type")).toString();
            if (key.isEmpty()) {
                errors->append(location + QStringLiteral(".key must be non-empty"));
            } else if (keys.contains(key)) {
                errors->append(QStringLiteral("duplicate settings key: %1").arg(key));
            } else {
                keys.insert(key);
            }
            if (!kFieldTypes.contains(type))
                errors->append(location + QStringLiteral(" has unsupported type: ") + type);
            const QJsonValue secret = field.value(QStringLiteral("secret"));
            if (!secret.isUndefined() && !secret.isBool())
                errors->append(location + QStringLiteral(".secret must be boolean"));
            if (type != QLatin1String("enum")) continue;
            const QJsonValue optionsValue = field.value(QStringLiteral("options"));
            if (!optionsValue.isArray() || optionsValue.toArray().isEmpty()) {
                errors->append(location + QStringLiteral(".options must be a non-empty array"));
                continue;
            }
            QSet<QString> values;
            for (const QJsonValue& option : optionsValue.toArray()) {
                const QString stableValue = option.isObject()
                    ? option.toObject().value(QStringLiteral("value")).toString()
                    : option.toString();
                if (stableValue.isEmpty() || values.contains(stableValue)) {
                    errors->append(location
                        + QStringLiteral(" contains an invalid or duplicate enum value"));
                    continue;
                }
                values.insert(stableValue);
            }
            const QJsonValue defaultValue = field.value(QStringLiteral("default"));
            if (!defaultValue.isUndefined()
                && (!defaultValue.isString()
                    || !values.contains(defaultValue.toString()))) {
                errors->append(location
                    + QStringLiteral(".default is not an enum stable value"));
            }
        }
    }
}

QSet<QString> validateCatalog(const QString& path, const QString& locale,
                              const QString& prefix, QStringList* errors) {
    const QJsonObject root = readObject(path, errors);
    if (root.isEmpty()) return {};
    if (root.value(QStringLiteral("locale")).toString() != locale
        || !root.value(QStringLiteral("contexts")).isObject()) {
        errors->append(QStringLiteral("catalog locale/contexts is invalid: %1").arg(path));
        return {};
    }
    QSet<QString> result;
    const QJsonObject contexts = root.value(QStringLiteral("contexts")).toObject();
    for (auto context = contexts.begin(); context != contexts.end(); ++context) {
        result.insert(context.key());
        if (!context.key().startsWith(prefix)) {
            errors->append(QStringLiteral("catalog context escapes %1: %2")
                               .arg(prefix, context.key()));
        }
        if (!context.value().isObject()) {
            errors->append(QStringLiteral("catalog context is not an object: %1")
                               .arg(context.key()));
            continue;
        }
        const QJsonObject messages = context.value().toObject();
        for (auto message = messages.begin(); message != messages.end(); ++message) {
            if (!message.value().isString()
                || (locale == QLatin1String("en-US")
                    && message.value().toString().isEmpty())) {
                errors->append(QStringLiteral("catalog message is invalid: %1/%2")
                                   .arg(context.key(), message.key()));
            }
        }
    }
    return result;
}

void validateI18n(const QString& pluginDir, const QString& pluginId,
                  QStringList* errors, QStringList* warnings) {
    const QDir i18n(QDir(pluginDir).filePath(QStringLiteral("i18n")));
    if (!i18n.exists()) {
        warnings->append(QStringLiteral("i18n directory is missing"));
        return;
    }
    const QString prefix = QStringLiteral("plugin.%1.").arg(pluginId);
    QHash<QString, QSet<QString>> contexts;
    for (const QString& locale : {QStringLiteral("zh-CN"), QStringLiteral("en-US")}) {
        const QString path = i18n.filePath(locale + QStringLiteral(".json"));
        if (!QFile::exists(path)) {
            errors->append(QStringLiteral("catalog is missing: %1").arg(path));
            continue;
        }
        contexts.insert(locale, validateCatalog(path, locale, prefix, errors));
    }
    if (contexts.contains(QStringLiteral("zh-CN"))
        && contexts.contains(QStringLiteral("en-US"))
        && contexts.value(QStringLiteral("zh-CN"))
            != contexts.value(QStringLiteral("en-US"))) {
        errors->append(QStringLiteral("zh-CN and en-US catalog contexts differ"));
    }
}
} // namespace

PluginContractValidationResult PluginContractValidator::validate(
    const QString& pluginDir, const QString& runtime,
    const QString& discoveredBackend,
    const PluginValidationEnvironment& environment) {
    PluginContractValidationResult result;
    if (runtime == QLatin1String("native")) {
        result.manifest = PluginManifest::load(
            pluginDir, discoveredBackend, environment);
    } else if (runtime == QLatin1String("python")) {
        result.manifest = PythonPluginManifest::load(pluginDir, environment);
    } else {
        result.errors.append(QStringLiteral("runtime must be native or python"));
        return result;
    }
    if (!result.manifest.ok) return result;

    const QString manifestName = runtime == QLatin1String("native")
        ? QStringLiteral("metadata.json") : QStringLiteral("plugin.json");
    const QJsonObject object = readObject(
        QDir(pluginDir).filePath(manifestName), &result.errors);
    if (!object.isEmpty())
        validateSettings(object.value(QStringLiteral("settings")), &result.errors);
    validateI18n(pluginDir, result.manifest.descriptor.id,
                 &result.errors, &result.warnings);
    return result;
}
