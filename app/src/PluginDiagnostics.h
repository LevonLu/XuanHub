// PluginDiagnostics.h — 生成插件级脱敏诊断包并筛选对应日志
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct PluginDiagnosticsResult {
    bool ok = false;
    QString error;
    int logEntryCount = 0;
};

class PluginDiagnostics final {
public:
    static PluginDiagnosticsResult write(
        const QString& outputPath, const QString& logPath,
        const QVariantMap& plugin, const QVariantMap& health,
        const QVariantMap& managedPackage, const QStringList& privateRoots,
        const QString& applicationVersion);

    static QString redactText(QString text, const QStringList& privateRoots);
    static QJsonArray filteredLogEntries(
        const QString& logPath, const QString& pluginId,
        const QStringList& privateRoots, int maximumEntries = 300);

private:
    static QJsonValue redactJson(const QJsonValue& value,
                                 const QStringList& privateRoots);
};
