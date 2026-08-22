// PluginManifest.cpp — Manifest 字段、兼容范围和目录边界校验实现
#include "PluginManifest.h"

#include "SemanticVersion.h"
#include "barplugin/PluginPathValidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

namespace {
const QRegularExpression kPluginIdPattern(
    QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));

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
        if (!item.isString() || item.toString().trimmed().isEmpty()) {
            *error = QStringLiteral("manifest field '%1' must contain non-empty strings")
                         .arg(key);
            return false;
        }
        const QString text = item.toString().trimmed();
        if (seen.contains(text)) {
            *error = QStringLiteral("manifest field '%1' contains duplicate '%2'")
                         .arg(key, text);
            return false;
        }
        seen.insert(text);
        output->append(text);
    }
    return true;
}

QString normalizedArchitecture(QString architecture) {
    architecture = architecture.trimmed().toLower();
    if (architecture == QLatin1String("x86_64")
        || architecture == QLatin1String("amd64")) {
        return QStringLiteral("x64");
    }
    if (architecture == QLatin1String("i386")
        || architecture == QLatin1String("i686")) {
        return QStringLiteral("x86");
    }
    if (architecture == QLatin1String("arm64")) return QStringLiteral("arm64");
    return architecture;
}

bool sameCompatibilityValue(const QString& expected, const QString& actual) {
    return expected == QLatin1String("*")
        || expected.compare(actual, Qt::CaseInsensitive) == 0;
}

bool validateHostRange(const xuanhub::PluginDescriptor& descriptor,
                       const QString& hostVersion, QString* error) {
    SemanticVersion host;
    if (!SemanticVersion::parse(hostVersion, &host, error)) {
        *error = QStringLiteral("host version is invalid: %1").arg(*error);
        return false;
    }
    if (!descriptor.minHostVersion.isEmpty()) {
        SemanticVersion minimum;
        if (!SemanticVersion::parse(descriptor.minHostVersion, &minimum, error)) {
            *error = QStringLiteral("minHostVersion is invalid: %1").arg(*error);
            return false;
        }
        if (host.compare(minimum) < 0) {
            *error = QStringLiteral("host %1 is below minimum %2")
                         .arg(hostVersion, descriptor.minHostVersion);
            return false;
        }
    }
    if (!descriptor.maxHostVersion.isEmpty()) {
        const QString maximumText = descriptor.maxHostVersion;
        if (maximumText.contains(QLatin1Char('x'), Qt::CaseInsensitive)
            || maximumText.contains(QLatin1Char('*'))) {
            if (!SemanticVersion::isValidConstraint(maximumText, error)) {
                *error = QStringLiteral("maxHostVersion is invalid: %1").arg(*error);
                return false;
            }
            if (!SemanticVersion::matches(hostVersion, maximumText)) {
                *error = QStringLiteral("host %1 is outside maximum range %2")
                             .arg(hostVersion, maximumText);
                return false;
            }
        } else {
            SemanticVersion maximum;
            if (!SemanticVersion::parse(maximumText, &maximum, error)) {
                *error = QStringLiteral("maxHostVersion is invalid: %1").arg(*error);
                return false;
            }
            if (host.compare(maximum) > 0) {
                *error = QStringLiteral("host %1 is above maximum %2")
                             .arg(hostVersion, maximumText);
                return false;
            }
        }
    }
    return true;
}

bool parseDependencies(const QJsonObject& object,
                       QVector<xuanhub::PluginDependency>* output,
                       QString* error) {
    const QJsonValue value = object.value(QStringLiteral("dependencies"));
    if (value.isUndefined()) return true;
    if (!value.isArray()) {
        *error = QStringLiteral("manifest field 'dependencies' must be an array");
        return false;
    }
    QSet<QString> seen;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isObject()) {
            *error = QStringLiteral("manifest dependency must be an object");
            return false;
        }
        const QJsonObject dependencyObject = item.toObject();
        xuanhub::PluginDependency dependency;
        if (!requiredString(dependencyObject, QStringLiteral("id"),
                            &dependency.id, error)
            || !kPluginIdPattern.match(dependency.id).hasMatch()) {
            if (error->isEmpty())
                *error = QStringLiteral("dependency id is invalid: %1").arg(dependency.id);
            return false;
        }
        if (seen.contains(dependency.id)) {
            *error = QStringLiteral("duplicate dependency id: %1").arg(dependency.id);
            return false;
        }
        seen.insert(dependency.id);
        if (!optionalString(dependencyObject, QStringLiteral("version"),
                            &dependency.versionConstraint, error)) {
            return false;
        }
        if (dependency.versionConstraint.isEmpty())
            dependency.versionConstraint = QStringLiteral("*");
        if (!SemanticVersion::isValidConstraint(dependency.versionConstraint, error)) {
            *error = QStringLiteral("dependency '%1' has invalid version: %2")
                         .arg(dependency.id, *error);
            return false;
        }
        const QJsonValue optional = dependencyObject.value(QStringLiteral("optional"));
        if (!optional.isUndefined() && !optional.isBool()) {
            *error = QStringLiteral("dependency '%1' optional must be boolean")
                         .arg(dependency.id);
            return false;
        }
        dependency.optional = optional.toBool(false);
        output->append(dependency);
    }
    return true;
}
} // namespace

PluginValidationEnvironment PluginValidationEnvironment::current() {
    PluginValidationEnvironment environment;
    environment.hostVersion = QCoreApplication::applicationVersion();
    environment.architecture = normalizedArchitecture(QSysInfo::currentCpuArchitecture());
    environment.qtVersion = QString::fromLatin1(qVersion());
#if defined(_MSC_VER)
    environment.compiler = _MSC_VER >= 1930
        ? QStringLiteral("msvc2022") : QStringLiteral("msvc");
#elif defined(__MINGW32__)
    environment.compiler = QStringLiteral("mingw");
#else
    environment.compiler = QStringLiteral("unknown");
#endif
#if defined(_DLL)
    environment.crt = QStringLiteral("dynamic");
#else
    environment.crt = QStringLiteral("static");
#endif
    return environment;
}

PluginManifestResult PluginManifest::load(
    const QString& pluginDir, const QString& discoveredBackend,
    const PluginValidationEnvironment& environment) {
    const QString manifestPath = QDir(pluginDir).absoluteFilePath(
        QStringLiteral("metadata.json"));
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
        return failed(QStringLiteral("cannot open manifest: %1").arg(manifestPath));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failed(QStringLiteral("invalid manifest JSON: %1")
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
    if (!kPluginIdPattern.match(descriptor.id).hasMatch())
        return failed(QStringLiteral("plugin id is invalid: %1").arg(descriptor.id));
    if (!SemanticVersion::parse(descriptor.version, nullptr, &error))
        return failed(QStringLiteral("plugin version is invalid: %1").arg(error));

    const QJsonValue apiVersion = object.value(QStringLiteral("apiVersion"));
    if (!apiVersion.isUndefined()) {
        if (!apiVersion.isDouble() || apiVersion.toDouble() != apiVersion.toInt())
            return failed(QStringLiteral("apiVersion must be an integer"));
        descriptor.apiVersion = apiVersion.toInt();
    }
    if (descriptor.apiVersion != 1 && descriptor.apiVersion != 2)
        return failed(QStringLiteral("unsupported apiVersion: %1").arg(descriptor.apiVersion));

    if (!optionalString(object, QStringLiteral("minHostVersion"),
                        &descriptor.minHostVersion, &error)
        || !optionalString(object, QStringLiteral("maxHostVersion"),
                           &descriptor.maxHostVersion, &error)
        || !optionalString(object, QStringLiteral("author"), &descriptor.author, &error)) {
        return failed(error);
    }
    if (!validateHostRange(descriptor, environment.hostVersion, &error))
        return failed(error);
    if (!stringArray(object, QStringLiteral("capabilities"),
                     &descriptor.capabilities, &error)
        || !stringArray(object, QStringLiteral("permissions"),
                        &descriptor.permissions, &error)
        || !parseDependencies(object, &descriptor.dependencies, &error)) {
        return failed(error);
    }

    if (descriptor.apiVersion == 2) {
        const QJsonValue entryValue = object.value(QStringLiteral("entry"));
        if (!entryValue.isObject())
            return failed(QStringLiteral("V2 manifest field 'entry' must be an object"));
        const QJsonObject entry = entryValue.toObject();
        if (!requiredString(entry, QStringLiteral("backend"),
                            &descriptor.entry.backend, &error)
            || !requiredString(entry, QStringLiteral("qml"),
                               &descriptor.entry.qml, &error)
            || !optionalString(entry, QStringLiteral("settingsQml"),
                               &descriptor.entry.settingsQml, &error)) {
            return failed(error);
        }
        if (QFileInfo(descriptor.entry.backend).fileName()
            .compare(discoveredBackend, Qt::CaseInsensitive) != 0) {
            return failed(QStringLiteral("manifest backend '%1' does not match discovered '%2'")
                              .arg(descriptor.entry.backend, discoveredBackend));
        }

        const QJsonValue compatibilityValue = object.value(QStringLiteral("compatibility"));
        if (!compatibilityValue.isObject()) {
            return failed(QStringLiteral(
                "V2 manifest field 'compatibility' must be an object"));
        }
        const QJsonObject compatibility = compatibilityValue.toObject();
        if (!requiredString(compatibility, QStringLiteral("architecture"),
                            &descriptor.compatibility.architecture, &error)
            || !requiredString(compatibility, QStringLiteral("qtVersion"),
                               &descriptor.compatibility.qtVersion, &error)
            || !requiredString(compatibility, QStringLiteral("compiler"),
                               &descriptor.compatibility.compiler, &error)
            || !requiredString(compatibility, QStringLiteral("crt"),
                               &descriptor.compatibility.crt, &error)) {
            return failed(error);
        }
        if (!sameCompatibilityValue(
                normalizedArchitecture(descriptor.compatibility.architecture),
                normalizedArchitecture(environment.architecture))) {
            return failed(QStringLiteral("plugin architecture '%1' does not match host '%2'")
                              .arg(descriptor.compatibility.architecture,
                                   environment.architecture));
        }
        if (!sameCompatibilityValue(descriptor.compatibility.qtVersion,
                                    environment.qtVersion)
            || !sameCompatibilityValue(descriptor.compatibility.compiler,
                                       environment.compiler)
            || !sameCompatibilityValue(descriptor.compatibility.crt,
                                       environment.crt)) {
            return failed(QStringLiteral("plugin native ABI does not match host"));
        }
    } else {
        descriptor.entry.backend = discoveredBackend;
        if (!optionalString(object, QStringLiteral("settingsQml"),
                            &descriptor.entry.settingsQml, &error)) {
            return failed(error);
        }
    }

    if (!xuanhub::validatePluginFilePath(pluginDir, QStringLiteral("backend"),
                                         descriptor.entry.backend, &error)) {
        return failed(error);
    }
    if (!descriptor.entry.qml.isEmpty()
        && !xuanhub::validatePluginFilePath(pluginDir, QStringLiteral("qml"),
                                            descriptor.entry.qml, &error)) {
        return failed(error);
    }
    if (!descriptor.entry.settingsQml.isEmpty()
        && !xuanhub::validatePluginFilePath(pluginDir, QStringLiteral("settingsQml"),
                                            descriptor.entry.settingsQml, &error)) {
        return failed(error);
    }

    PluginManifestResult result;
    result.ok = true;
    result.descriptor = descriptor;
    return result;
}
