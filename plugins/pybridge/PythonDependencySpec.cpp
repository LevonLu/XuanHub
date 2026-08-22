// PythonDependencySpec.cpp — 校验并规范化 Python 版本和包依赖声明
#include "PythonDependencySpec.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace {
void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

bool isValidVersion(const QString& version) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(0|[1-9]\d*)(?:\.(0|[1-9]\d*)){1,3}$)"));
    return pattern.match(version).hasMatch();
}
} // namespace

bool isValidPythonVersionConstraint(const QString& constraint, QString* error) {
    const QString trimmed = constraint.trimmed();
    if (trimmed == QLatin1String("*")) {
        if (error) error->clear();
        return true;
    }
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("version constraint is empty"));
        return false;
    }

    static const QRegularExpression itemPattern(
        QStringLiteral(R"(^(==|!=|>=|<=|>|<)\s*(.+)$)"));
    QSet<QString> seen;
    for (const QString& rawItem : trimmed.split(QLatin1Char(','))) {
        const QString item = rawItem.trimmed();
        const auto match = itemPattern.match(item);
        if (!match.hasMatch() || !isValidVersion(match.captured(2).trimmed())) {
            setError(error, QStringLiteral("invalid version constraint item: %1").arg(item));
            return false;
        }
        const QString normalized = match.captured(1) + match.captured(2).trimmed();
        if (seen.contains(normalized)) {
            setError(error, QStringLiteral("duplicate version constraint item: %1")
                                .arg(normalized));
            return false;
        }
        seen.insert(normalized);
    }
    if (error) error->clear();
    return true;
}

bool readPythonDependencySpec(const QJsonObject& manifest,
                              PythonDependencySpec* output,
                              QString* error) {
    PythonDependencySpec parsed;
    const QJsonValue pythonValue = manifest.value(QStringLiteral("python"));
    if (pythonValue.isUndefined()) {
        if (output) *output = parsed;
        if (error) error->clear();
        return true;
    }
    if (!pythonValue.isObject()) {
        setError(error, QStringLiteral("python must be an object"));
        return false;
    }

    parsed.declared = true;
    const QJsonObject python = pythonValue.toObject();
    const QSet<QString> allowedKeys{QStringLiteral("version"),
                                    QStringLiteral("packages"),
                                    QStringLiteral("environment")};
    for (auto it = python.begin(); it != python.end(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            setError(error, QStringLiteral("python contains unknown field: %1").arg(it.key()));
            return false;
        }
    }

    const QJsonValue versionValue = python.value(QStringLiteral("version"));
    if (!versionValue.isUndefined()) {
        if (!versionValue.isString()) {
            setError(error, QStringLiteral("python.version must be a string"));
            return false;
        }
        parsed.versionConstraint = versionValue.toString().trimmed();
        QString constraintError;
        if (!isValidPythonVersionConstraint(parsed.versionConstraint, &constraintError)) {
            setError(error, QStringLiteral("python.version %1").arg(constraintError));
            return false;
        }
    }

    const QJsonValue packagesValue = python.value(QStringLiteral("packages"));
    if (!packagesValue.isUndefined() && !packagesValue.isArray()) {
        setError(error, QStringLiteral("python.packages must be an array"));
        return false;
    }

    static const QRegularExpression packagePattern(
        QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$)"));
    static const QRegularExpression importPattern(
        QStringLiteral(R"(^[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*$)"));
    QSet<QString> packageNames;
    for (const QJsonValue& value : packagesValue.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("python package declaration must be an object"));
            return false;
        }
        const QJsonObject object = value.toObject();
        const QSet<QString> packageKeys{QStringLiteral("name"),
                                        QStringLiteral("import"),
                                        QStringLiteral("version"),
                                        QStringLiteral("native")};
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!packageKeys.contains(it.key())) {
                setError(error, QStringLiteral("python package contains unknown field: %1")
                                    .arg(it.key()));
                return false;
            }
        }

        PythonPackageRequirement requirement;
        requirement.name = object.value(QStringLiteral("name")).toString().trimmed();
        requirement.importName = object.value(QStringLiteral("import")).toString().trimmed();
        if (requirement.importName.isEmpty())
            requirement.importName = requirement.name.section(QLatin1Char('.'), 0, 0)
                                         .replace(QLatin1Char('-'), QLatin1Char('_'));
        if (!packagePattern.match(requirement.name).hasMatch()
            || !importPattern.match(requirement.importName).hasMatch()) {
            setError(error, QStringLiteral("python package has invalid name or import"));
            return false;
        }
        const QString normalizedName = requirement.name.toLower()
                                           .replace(QLatin1Char('_'), QLatin1Char('-'));
        if (packageNames.contains(normalizedName)) {
            setError(error, QStringLiteral("python package is duplicated: %1")
                                .arg(requirement.name));
            return false;
        }
        packageNames.insert(normalizedName);

        const QJsonValue packageVersion = object.value(QStringLiteral("version"));
        if (!packageVersion.isUndefined()) {
            if (!packageVersion.isString()) {
                setError(error, QStringLiteral("python package version must be a string"));
                return false;
            }
            requirement.versionConstraint = packageVersion.toString().trimmed();
            QString constraintError;
            if (!isValidPythonVersionConstraint(requirement.versionConstraint,
                                                &constraintError)) {
                setError(error, QStringLiteral("python package '%1' %2")
                                    .arg(requirement.name, constraintError));
                return false;
            }
        }
        const QJsonValue nativeValue = object.value(QStringLiteral("native"));
        if (!nativeValue.isUndefined() && !nativeValue.isBool()) {
            setError(error, QStringLiteral("python package native must be boolean"));
            return false;
        }
        requirement.native = nativeValue.toBool(false);
        parsed.packages.append(requirement);
    }

    const QJsonValue environmentValue = python.value(QStringLiteral("environment"));
    if (!environmentValue.isUndefined()) {
        if (!environmentValue.isObject()) {
            setError(error, QStringLiteral("python.environment must be an object"));
            return false;
        }
        const QJsonObject environment = environmentValue.toObject();
        const QSet<QString> environmentKeys{QStringLiteral("mode"),
                                            QStringLiteral("wheels")};
        for (auto it = environment.begin(); it != environment.end(); ++it) {
            if (!environmentKeys.contains(it.key())) {
                setError(error, QStringLiteral("python.environment contains unknown field: %1")
                                    .arg(it.key()));
                return false;
            }
        }
        const QJsonValue modeValue = environment.value(QStringLiteral("mode"));
        if (!modeValue.isUndefined() && !modeValue.isString()) {
            setError(error, QStringLiteral("python.environment.mode must be a string"));
            return false;
        }
        parsed.environmentMode = modeValue.toString(QStringLiteral("shared")).trimmed();
        if (parsed.environmentMode != QStringLiteral("shared")
            && parsed.environmentMode != QStringLiteral("private")) {
            setError(error, QStringLiteral("python.environment.mode must be shared or private"));
            return false;
        }
        const QJsonValue wheelsValue = environment.value(QStringLiteral("wheels"));
        if (!wheelsValue.isUndefined() && !wheelsValue.isString()) {
            setError(error, QStringLiteral("python.environment.wheels must be a string"));
            return false;
        }
        parsed.wheelDirectory = wheelsValue.toString().trimmed();
        if (!parsed.wheelDirectory.isEmpty()) {
            QString normalized = parsed.wheelDirectory;
            const bool absolute = QRegularExpression(
                QStringLiteral(R"(^[A-Za-z]:[\\/])")).match(normalized).hasMatch()
                || normalized.startsWith(QLatin1Char('/'))
                || normalized.startsWith(QStringLiteral("\\\\"));
            const QStringList segments = normalized.replace(QLatin1Char('\\'), QLatin1Char('/'))
                                             .split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (absolute || normalized.contains(QLatin1Char(':')) || segments.isEmpty()
                || segments.contains(QStringLiteral(".."))
                || segments.contains(QStringLiteral("."))) {
                setError(error, QStringLiteral(
                    "python.environment.wheels must be a safe relative directory"));
                return false;
            }
            if (parsed.environmentMode != QStringLiteral("private")) {
                setError(error, QStringLiteral(
                    "python.environment.wheels requires private mode"));
                return false;
            }
        }
    }

    if (output) *output = parsed;
    if (error) error->clear();
    return true;
}
