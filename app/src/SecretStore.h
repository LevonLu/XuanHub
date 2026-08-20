// SecretStore.h — 使用 Windows DPAPI 保存插件敏感配置，不把明文写入 config.json
#pragma once

#include <QJsonValue>
#include <QString>

#include <optional>

class SecretStore {
public:
    explicit SecretStore(QString directory);

    // secretId 固定为 plugin/<plugin-id>/<field-key>，避免插件互相引用密钥。
    static QString idForPluginField(const QString& pluginId, const QString& fieldKey);
    static QJsonValue referenceValue(const QString& secretId);
    static QString referenceId(const QJsonValue& value);

    // 空值表示密钥不存在、已清空或无法解密；调用方不得把它写回普通配置。
    std::optional<QString> read(const QString& secretId) const;
    bool write(const QString& secretId, const QString& plaintext) const;

private:
    QString m_path;
};
