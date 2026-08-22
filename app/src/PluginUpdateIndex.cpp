// PluginUpdateIndex.cpp — 对更新索引做 URL、身份、版本、哈希与大小的 fail-closed 校验
#include "PluginUpdateIndex.h"

#include "SemanticVersion.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
// 与 PluginPackageValidator::MaximumArchiveBytes 保持一致；下载阶段仍会再次进入统一校验器。
constexpr qint64 kMaximumPackageBytes = 256LL * 1024 * 1024;

int compareVersions(const QString& left, const QString& right) {
    SemanticVersion leftVersion;
    SemanticVersion rightVersion;
    if (!SemanticVersion::parse(left, &leftVersion)
        || !SemanticVersion::parse(right, &rightVersion)) {
        return 0;
    }
    return leftVersion.compare(rightVersion);
}

bool safeUrl(const QUrl& url, QString* error) {
    if (!url.isValid() || url.isRelative() || !url.userInfo().isEmpty()
        || url.hasFragment()
        || (url.scheme() != QLatin1String("https")
            && url.scheme() != QLatin1String("file"))) {
        *error = QStringLiteral("update URL must be an absolute HTTPS or file URL without credentials or fragment");
        return false;
    }
    if (url.scheme() == QLatin1String("file") && !url.isLocalFile()) {
        *error = QStringLiteral("file update URL must resolve to a local path");
        return false;
    }
    return true;
}

bool requiredString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        *error = QStringLiteral("update field '%1' must be a non-empty string").arg(key);
        return false;
    }
    *output = value.toString().trimmed();
    return true;
}
} // namespace

QVariantMap PluginUpdateCandidate::toVariantMap() const {
    return {
        {QStringLiteral("sourceId"), sourceId},
        {QStringLiteral("sourceUrl"), sourceUrl.toString(QUrl::FullyEncoded)},
        {QStringLiteral("id"), id},
        {QStringLiteral("version"), version},
        {QStringLiteral("runtime"), runtime},
        {QStringLiteral("packageUrl"), packageUrl.toString(QUrl::FullyEncoded)},
        {QStringLiteral("packageSha256"), packageSha256},
        {QStringLiteral("packageSize"), packageSize},
        {QStringLiteral("signerKeyId"), signerKeyId},
    };
}

PluginUpdateIndexResult PluginUpdateIndex::parse(const QByteArray& json,
                                                  const QUrl& sourceUrl) {
    PluginUpdateIndexResult result;
    if (!validateSourceUrl(sourceUrl, &result.error)) return result;
    if (json.isEmpty() || json.size() > 4 * 1024 * 1024) {
        result.error = QStringLiteral("update index is empty or exceeds 4 MiB");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("invalid update index JSON: %1")
                           .arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = document.object();
    const QJsonValue schema = root.value(QStringLiteral("schemaVersion"));
    if (!schema.isDouble() || schema.toDouble() != 1.0) {
        result.error = QStringLiteral("unsupported update index schemaVersion");
        return result;
    }
    if (!requiredString(root, QStringLiteral("id"), &result.sourceId, &result.error))
        return result;
    static const QRegularExpression idPattern(
        QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
    if (!idPattern.match(result.sourceId).hasMatch()) {
        result.error = QStringLiteral("update source id is invalid");
        return result;
    }
    const QJsonValue packagesValue = root.value(QStringLiteral("packages"));
    if (!packagesValue.isArray() || packagesValue.toArray().size() > 4096) {
        result.error = QStringLiteral("update index packages must be an array with at most 4096 items");
        return result;
    }

    static const QRegularExpression hashPattern(
        QStringLiteral(R"(^sha256:[0-9a-f]{64}$)"));
    QSet<QString> seen;
    for (const QJsonValue& value : packagesValue.toArray()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("update package entry must be an object");
            return result;
        }
        const QJsonObject object = value.toObject();
        PluginUpdateCandidate candidate;
        candidate.sourceId = result.sourceId;
        candidate.sourceUrl = sourceUrl;
        QString packageUrlText;
        QString error;
        if (!requiredString(object, QStringLiteral("id"), &candidate.id, &error)
            || !requiredString(object, QStringLiteral("version"), &candidate.version, &error)
            || !requiredString(object, QStringLiteral("runtime"), &candidate.runtime, &error)
            || !requiredString(object, QStringLiteral("url"), &packageUrlText, &error)
            || !requiredString(object, QStringLiteral("sha256"),
                               &candidate.packageSha256, &error)) {
            result.error = error;
            return result;
        }
        if (!idPattern.match(candidate.id).hasMatch()) {
            result.error = QStringLiteral("update package id is invalid");
            return result;
        }
        if (!SemanticVersion::parse(candidate.version, nullptr, &error)) {
            result.error = QStringLiteral("update package version is invalid: %1").arg(error);
            return result;
        }
        if (candidate.runtime != QLatin1String("native")
            && candidate.runtime != QLatin1String("python")) {
            result.error = QStringLiteral("update package runtime is invalid");
            return result;
        }
        if (!hashPattern.match(candidate.packageSha256).hasMatch()) {
            result.error = QStringLiteral("update package sha256 is invalid");
            return result;
        }
        const QJsonValue sizeValue = object.value(QStringLiteral("size"));
        if (!sizeValue.isDouble() || sizeValue.toDouble() != sizeValue.toInteger()
            || sizeValue.toInteger() <= 0
            || sizeValue.toInteger() > kMaximumPackageBytes) {
            result.error = QStringLiteral("update package size is invalid");
            return result;
        }
        candidate.packageSize = sizeValue.toInteger();
        const QJsonValue signer = object.value(QStringLiteral("signerKeyId"));
        if (!signer.isUndefined()) {
            if (!signer.isString()
                || !hashPattern.match(signer.toString()).hasMatch()) {
                result.error = QStringLiteral("update package signerKeyId is invalid");
                return result;
            }
            candidate.signerKeyId = signer.toString();
        }
        candidate.packageUrl = sourceUrl.resolved(QUrl(packageUrlText));
        if (!validateSourceUrl(candidate.packageUrl, &result.error)) return result;
        const QString duplicateKey = candidate.id + QLatin1Char('\n') + candidate.version;
        if (seen.contains(duplicateKey)) {
            result.error = QStringLiteral("update index contains duplicate plugin version");
            return result;
        }
        seen.insert(duplicateKey);
        result.packages.append(candidate);
    }
    result.ok = true;
    return result;
}

bool PluginUpdateIndex::validateSourceUrl(const QUrl& url, QString* error) {
    QString localError;
    const bool ok = safeUrl(url, &localError);
    if (error) *error = localError;
    return ok;
}

QList<PluginUpdateCandidate> PluginUpdateIndex::newerCandidates(
    const QList<PluginUpdateCandidate>& candidates,
    const QHash<QString, QString>& installedVersions) {
    QHash<QString, PluginUpdateCandidate> newest;
    for (const PluginUpdateCandidate& candidate : candidates) {
        const QString installed = installedVersions.value(candidate.id);
        if (installed.isEmpty()
            || compareVersions(candidate.version, installed) <= 0) {
            continue;
        }
        const auto current = newest.constFind(candidate.id);
        if (current == newest.cend()
            || compareVersions(candidate.version, current->version) > 0) {
            newest.insert(candidate.id, candidate);
        }
    }
    QList<PluginUpdateCandidate> result = newest.values();
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return result;
}
