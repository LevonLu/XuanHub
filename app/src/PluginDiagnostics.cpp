// PluginDiagnostics.cpp — 诊断包只导出必要运行信息，配置和密钥从不进入数据流
#include "PluginDiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
constexpr qint64 kMaximumLogBytes = 4 * 1024 * 1024;

QString codeForLevel(const QString& level) {
    if (level == QLatin1String("F") || level == QLatin1String("C"))
        return QStringLiteral("plugin.log.critical");
    if (level == QLatin1String("W")) return QStringLiteral("plugin.log.warning");
    if (level == QLatin1String("I")) return QStringLiteral("plugin.log.info");
    return QStringLiteral("plugin.log.debug");
}

bool isPluginIdCharacter(QChar character) {
    return character.isLetterOrNumber() || character == u'_'
        || character == u'.' || character == u'-';
}

bool containsPluginId(const QString& line, const QString& pluginId) {
    qsizetype from = 0;
    while (from < line.size()) {
        const qsizetype position = line.indexOf(pluginId, from, Qt::CaseInsensitive);
        if (position < 0) return false;
        const qsizetype end = position + pluginId.size();
        const bool leftBoundary = position == 0
            || !isPluginIdCharacter(line.at(position - 1));
        const bool rightBoundary = end == line.size()
            || !isPluginIdCharacter(line.at(end));
        if (leftBoundary && rightBoundary) return true;
        from = position + 1;
    }
    return false;
}
} // namespace

QString PluginDiagnostics::redactText(QString text, const QStringList& privateRoots) {
    for (const QString& root : privateRoots) {
        const QString normalized = root.trimmed();
        if (normalized.isEmpty()) continue;
        text.replace(QRegularExpression(QRegularExpression::escape(normalized),
                                        QRegularExpression::CaseInsensitiveOption),
                     QStringLiteral("<private-path>"));
    }

    static const QRegularExpression windowsProfile(
        QStringLiteral(R"([A-Za-z]:\\Users\\[^\\\s\"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression unixProfile(
        QStringLiteral(R"(/home/[^/\s\"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bearer(
        QStringLiteral(R"((Bearer\s+)[A-Za-z0-9._~+/=-]{8,})"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression secretAssignment(
        QStringLiteral(
            R"(((?:api[_-]?key|access[_-]?token|refresh[_-]?token|token|password|passwd|cookie|secret)\s*[:=]\s*[\"']?)[^\s,\"';\]\}]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression providerToken(
        QStringLiteral(R"(\b(?:sk|ghp|github_pat)-[A-Za-z0-9_-]{8,}\b)"),
        QRegularExpression::CaseInsensitiveOption);

    text.replace(windowsProfile, QStringLiteral("<private-path>"));
    text.replace(unixProfile, QStringLiteral("<private-path>"));
    text.replace(bearer, QStringLiteral("\\1<redacted>"));
    text.replace(secretAssignment, QStringLiteral("\\1<redacted>"));
    text.replace(providerToken, QStringLiteral("<redacted>"));
    return text;
}

QJsonValue PluginDiagnostics::redactJson(const QJsonValue& value,
                                         const QStringList& privateRoots) {
    if (value.isString()) return redactText(value.toString(), privateRoots);
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue& item : value.toArray())
            result.append(redactJson(item, privateRoots));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            result.insert(it.key(), redactJson(it.value(), privateRoots));
        return result;
    }
    return value;
}

QJsonArray PluginDiagnostics::filteredLogEntries(
    const QString& logPath, const QString& pluginId,
    const QStringList& privateRoots, int maximumEntries) {
    QJsonArray result;
    if (pluginId.isEmpty() || maximumEntries <= 0) return result;

    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return result;
    const bool readTailOnly = file.size() > kMaximumLogBytes;
    if (readTailOnly) file.seek(file.size() - kMaximumLogBytes);
    QByteArray bytes = file.readAll();
    if (readTailOnly) {
        const int firstNewline = bytes.indexOf('\n');
        if (firstNewline >= 0) bytes.remove(0, firstNewline + 1);
    }

    const QRegularExpression linePattern(
        QStringLiteral(R"(^\[([^\]]+)\]\s+\[([DIWCF])\]\s+(.*)$)"));
    const QRegularExpression structuredCode(
        QStringLiteral(
            R"(\[(?:code=)?([a-z][a-z0-9_-]*(?:\.[a-z0-9_-]+)+)\])"),
        QRegularExpression::CaseInsensitiveOption);

    const QList<QByteArray> lines = bytes.split('\n');
    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || !containsPluginId(line, pluginId)) continue;
        const QRegularExpressionMatch lineMatch = linePattern.match(line);
        const QString timestamp = lineMatch.hasMatch() ? lineMatch.captured(1) : QString{};
        const QString level = lineMatch.hasMatch() ? lineMatch.captured(2)
                                                   : QStringLiteral("D");
        QString message = lineMatch.hasMatch() ? lineMatch.captured(3) : line;
        const QRegularExpressionMatch codeMatch = structuredCode.match(message);
        const QString errorCode = codeMatch.hasMatch()
            ? codeMatch.captured(1).toLower() : codeForLevel(level);
        message = redactText(message, privateRoots);
        result.append(QJsonObject{
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("level"), level},
            {QStringLiteral("errorCode"), errorCode},
            {QStringLiteral("message"), message},
        });
        while (result.size() > maximumEntries) result.removeFirst();
    }
    return result;
}

PluginDiagnosticsResult PluginDiagnostics::write(
    const QString& outputPath, const QString& logPath,
    const QVariantMap& plugin, const QVariantMap& health,
    const QVariantMap& managedPackage, const QStringList& privateRoots,
    const QString& applicationVersion) {
    PluginDiagnosticsResult result;
    const QString pluginId = plugin.value(QStringLiteral("id")).toString();
    if (pluginId.isEmpty() || outputPath.trimmed().isEmpty()) {
        result.error = QStringLiteral("diagnostics.invalid_argument");
        return result;
    }

    const QJsonArray logs = filteredLogEntries(
        logPath, pluginId, privateRoots);
    QJsonObject safePackage;
    static const QStringList packageKeys{
        QStringLiteral("managed"), QStringLiteral("version"),
        QStringLiteral("previousVersion"), QStringLiteral("packageHash"),
        QStringLiteral("signatureAlgorithm"), QStringLiteral("signerKeyId"),
        QStringLiteral("publisher"),
    };
    for (const QString& key : packageKeys) {
        if (managedPackage.contains(key))
            safePackage.insert(key, QJsonValue::fromVariant(managedPackage.value(key)));
    }

    QJsonObject document{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("generatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("application"), QJsonObject{
             {QStringLiteral("name"), QCoreApplication::applicationName()},
             {QStringLiteral("version"), applicationVersion},
        }},
        {QStringLiteral("plugin"), QJsonObject::fromVariantMap(plugin)},
        {QStringLiteral("health"), QJsonObject::fromVariantMap(health)},
        {QStringLiteral("managedPackage"), safePackage},
        {QStringLiteral("logs"), logs},
        {QStringLiteral("redaction"), QJsonObject{
             {QStringLiteral("applied"), true},
             {QStringLiteral("configurationIncluded"), false},
             {QStringLiteral("secretStoreIncluded"), false},
             {QStringLiteral("privatePathsIncluded"), false},
        }},
    };
    document = redactJson(document, privateRoots).toObject();

    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("diagnostics.open_failed");
        return result;
    }
    if (file.write(QJsonDocument(document).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        result.error = QStringLiteral("diagnostics.write_failed");
        return result;
    }
    result.ok = true;
    result.logEntryCount = logs.size();
    return result;
}
