// SemanticVersion.cpp — SemVer 解析、比较与约束匹配实现
#include "SemanticVersion.h"

#include <QRegularExpression>

namespace {
const QRegularExpression kVersionPattern(QStringLiteral(
    R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$)"));
const QRegularExpression kConstraintPattern(QStringLiteral(
    R"(^(>=|<=|>|<|=)?\s*(.+)$)"));
const QRegularExpression kMajorWildcardPattern(QStringLiteral(
    R"(^(0|[1-9]\d*)\.(?:x|\*)$)"),
    QRegularExpression::CaseInsensitiveOption);

void setError(QString* error, const QString& text) {
    if (error) *error = text;
}

int compareIdentifier(const QString& left, const QString& right) {
    bool leftNumeric = false;
    bool rightNumeric = false;
    const qulonglong leftNumber = left.toULongLong(&leftNumeric);
    const qulonglong rightNumber = right.toULongLong(&rightNumeric);
    if (leftNumeric && rightNumeric)
        return leftNumber < rightNumber ? -1 : leftNumber > rightNumber ? 1 : 0;
    if (leftNumeric != rightNumeric) return leftNumeric ? -1 : 1;
    return QString::compare(left, right, Qt::CaseSensitive);
}
} // namespace

bool SemanticVersion::parse(const QString& text, SemanticVersion* output,
                            QString* error) {
    const auto match = kVersionPattern.match(text.trimmed());
    if (!match.hasMatch()) {
        setError(error, QStringLiteral("invalid semantic version: %1").arg(text));
        return false;
    }
    SemanticVersion parsed;
    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = false;
    parsed.major = match.captured(1).toInt(&majorOk);
    parsed.minor = match.captured(2).toInt(&minorOk);
    parsed.patch = match.captured(3).toInt(&patchOk);
    if (!majorOk || !minorOk || !patchOk) {
        setError(error, QStringLiteral("semantic version component is too large: %1")
                            .arg(text));
        return false;
    }
    if (!match.captured(4).isEmpty()) {
        parsed.prerelease = match.captured(4).split(QLatin1Char('.'));
        for (const QString& identifier : parsed.prerelease) {
            bool numeric = false;
            identifier.toULongLong(&numeric);
            if (numeric && identifier.size() > 1
                && identifier.startsWith(QLatin1Char('0'))) {
                setError(error, QStringLiteral("numeric prerelease has a leading zero: %1")
                                    .arg(identifier));
                return false;
            }
        }
    }
    if (output) *output = parsed;
    if (error) error->clear();
    return true;
}

int SemanticVersion::compare(const SemanticVersion& other) const {
    if (major != other.major) return major < other.major ? -1 : 1;
    if (minor != other.minor) return minor < other.minor ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;
    if (prerelease.isEmpty() != other.prerelease.isEmpty())
        return prerelease.isEmpty() ? 1 : -1;
    const int count = qMin(prerelease.size(), other.prerelease.size());
    for (int i = 0; i < count; ++i) {
        const int result = compareIdentifier(prerelease.at(i), other.prerelease.at(i));
        if (result != 0) return result < 0 ? -1 : 1;
    }
    if (prerelease.size() == other.prerelease.size()) return 0;
    return prerelease.size() < other.prerelease.size() ? -1 : 1;
}

QString SemanticVersion::toString() const {
    QString result = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!prerelease.isEmpty()) result += QLatin1Char('-') + prerelease.join(QLatin1Char('.'));
    return result;
}

bool SemanticVersion::isValidConstraint(const QString& constraint, QString* error) {
    const QString trimmed = constraint.trimmed();
    if (trimmed == QLatin1String("*")) return true;
    if (kMajorWildcardPattern.match(trimmed).hasMatch()) return true;
    const auto match = kConstraintPattern.match(trimmed);
    SemanticVersion ignored;
    if (match.hasMatch() && parse(match.captured(2), &ignored, error)) return true;
    if (error && error->isEmpty())
        *error = QStringLiteral("invalid version constraint: %1").arg(constraint);
    return false;
}

bool SemanticVersion::matches(const QString& version, const QString& constraint,
                              QString* error) {
    SemanticVersion actual;
    if (!parse(version, &actual, error)) return false;
    const QString trimmed = constraint.trimmed();
    if (trimmed == QLatin1String("*")) return true;
    const auto wildcard = kMajorWildcardPattern.match(trimmed);
    if (wildcard.hasMatch()) return actual.major == wildcard.captured(1).toInt();

    const auto match = kConstraintPattern.match(trimmed);
    if (!match.hasMatch()) {
        setError(error, QStringLiteral("invalid version constraint: %1").arg(constraint));
        return false;
    }
    SemanticVersion expected;
    if (!parse(match.captured(2), &expected, error)) return false;
    const int comparison = actual.compare(expected);
    const QString op = match.captured(1);
    if (op == QLatin1String(">=")) return comparison >= 0;
    if (op == QLatin1String("<=")) return comparison <= 0;
    if (op == QLatin1String(">")) return comparison > 0;
    if (op == QLatin1String("<")) return comparison < 0;
    return comparison == 0;
}
