// PluginLog.cpp — 将每条插件日志规范为单行、可过滤且可机器解析的格式
#include "barplugin/PluginLog.h"

#include <QDebug>
#include <QRegularExpression>

namespace xuanhub {
namespace {
QString normalizedId(const QString& value) {
    static const QRegularExpression valid(QStringLiteral(R"(^[A-Za-z0-9_.-]+$)"));
    return valid.match(value).hasMatch() ? value : QStringLiteral("unknown");
}

QString normalizedCode(const QString& value) {
    static const QRegularExpression valid(
        QStringLiteral(R"(^[a-z][a-z0-9_-]*(?:\.[a-z0-9_-]+)+$)"));
    return valid.match(value).hasMatch() ? value : QStringLiteral("plugin.log.message");
}
} // namespace

QString PluginLog::format(const QString& pluginId, const QString& errorCode,
                          const QString& message) {
    QString singleLine = message;
    singleLine.replace(u'\r', u' ');
    singleLine.replace(u'\n', QStringLiteral("\\n"));
    return QStringLiteral("[Plugin:%1] [code=%2] %3")
        .arg(normalizedId(pluginId), normalizedCode(errorCode), singleLine);
}

void PluginLog::debug(const QString& pluginId, const QString& errorCode,
                      const QString& message) {
    qDebug().noquote() << format(pluginId, errorCode, message);
}

void PluginLog::info(const QString& pluginId, const QString& errorCode,
                     const QString& message) {
    qInfo().noquote() << format(pluginId, errorCode, message);
}

void PluginLog::warning(const QString& pluginId, const QString& errorCode,
                        const QString& message) {
    qWarning().noquote() << format(pluginId, errorCode, message);
}

void PluginLog::critical(const QString& pluginId, const QString& errorCode,
                         const QString& message) {
    qCritical().noquote() << format(pluginId, errorCode, message);
}

} // namespace xuanhub
