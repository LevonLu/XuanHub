// UsageHubMigration.h — 只在内存合并旧用量中心，保存仍由设置事务完成
#pragma once
#include "ConfigService.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace UsageHubMigration {
inline QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

inline QJsonArray legacySchema() {
    return QJsonDocument::fromJson(R"([{"fields":[{"key":"vendors","type":"array","itemKey":"secretId","items":{"type":"object","fields":[{"key":"apiKey","type":"string","secret":true}]}}]}])").array();
}

inline void merge(QList<PluginConfigEntry>& entries, const QString& exeDir) {
    const QString oldId = QStringLiteral("py_usage_hub");
    const QString newId = QStringLiteral("py_agent_usage");
    int oldIndex = -1, newIndex = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].id == oldId) oldIndex = i;
        if (entries[i].id == newId) newIndex = i;
    }
    if (oldIndex < 0) return;
    const auto legacy = entries[oldIndex];
    auto vendors = legacy.config.value(QStringLiteral("vendors")).toList();
    if (vendors.isEmpty()) {
        auto document = readObject(QDir(exeDir).filePath(QStringLiteral("legacy-usage-hub/user_config.json")));
        if (document.isEmpty()) document = readObject(QDir(exeDir).filePath(QStringLiteral("plugins_py/usage_hub/user_config.json")));
        vendors = document.value(QStringLiteral("vendors")).toArray().toVariantList();
    }
    const auto states = readObject(QDir(qEnvironmentVariable("APPDATA")).filePath(QStringLiteral("XuanHub/usage_hub_state.json")));
    PluginConfigEntry merged = newIndex >= 0 ? entries[newIndex] : legacy;
    merged.id = newId;
    if (newIndex < 0) merged.config.remove(QStringLiteral("vendors"));
    auto accounts = merged.config.value(QStringLiteral("accounts")).toList();
    QVariantList barAccounts;
    QSet<QString> known;
    for (const auto& value : accounts) known.insert(value.toMap().value(QStringLiteral("id")).toString());
    for (int i = 0; i < vendors.size(); ++i) {
        auto account = vendors[i].toMap();
        auto identity = account.value(QStringLiteral("secretId")).toString();
        if (identity.isEmpty()) identity = QString::fromLatin1(QCryptographicHash::hash((QString::number(i) + account.value(QStringLiteral("id")).toString()).toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
        identity.prepend(QStringLiteral("legacy-"));
        if (known.contains(identity)) continue;
        const auto mode = account.value(QStringLiteral("mode"), QStringLiteral("balance")).toString();
        const auto provider = mode == QLatin1String("balance") ? QStringLiteral("deepseek") : mode == QLatin1String("plan") ? QStringLiteral("glm") : mode == QLatin1String("go") ? QStringLiteral("opencode-go") : QString();
        if (provider.isEmpty()) continue;
        const auto oldVendorId = account.value(QStringLiteral("id")).toString();
        account.insert(QStringLiteral("id"), identity);
        account.insert(QStringLiteral("providerId"), provider);
        account.insert(QStringLiteral("credential"), account.take(QStringLiteral("apiKey")).toString());
        account.insert(QStringLiteral("authMode"), mode == QLatin1String("go") ? QStringLiteral("cookie") : QStringLiteral("key"));
        if (!account.contains(QStringLiteral("startHour"))) account.insert(QStringLiteral("startHour"), 8);
        if (!account.contains(QStringLiteral("startMin"))) account.insert(QStringLiteral("startMin"), 30);
        // 老状态没有币种与凭据指纹，只传给新采集器做保守校验，不在宿主当作已确认金额。
        account.insert(QStringLiteral("legacyState"), states.value(oldVendorId).toObject().toVariantMap());
        accounts.append(account);
        known.insert(identity);
        if (account.value(QStringLiteral("enabled"), true).toBool()) barAccounts.append(QVariantMap{{QStringLiteral("id"), identity}, {QStringLiteral("providerId"), provider}});
    }
    merged.config.insert(QStringLiteral("accounts"), accounts);
    merged.config.insert(QStringLiteral("legacyBarAccounts"), barAccounts);
    merged.config.insert(QStringLiteral("legacyImportPending"), true);
    // 保留新插件原版本，令其自身迁移器生成列；只有旧 Hub 的配置也从 V1 开始。
    if (newIndex < 0) merged.configVersion = 1;
    entries.removeAt(oldIndex);
    if (newIndex >= 0) entries[newIndex > oldIndex ? newIndex - 1 : newIndex] = merged;
    else entries.append(merged);
}
}
