// SecretStore.h — 使用 Windows DPAPI 保存插件敏感配置，不把明文写入 config.json
#pragma once

#include <QJsonValue>
#include <QJsonObject>
#include <QString>

#include <optional>

class SecretStore {
public:
    class Transaction {
    public:
        explicit Transaction(QString path);
        ~Transaction();

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        bool stageWrite(const QString& secretId, const QString& plaintext);
        bool commit();
        bool rollback();
        void finalize() { m_finalized = true; }
        bool hasChanges() const { return m_changed; }

    private:
        bool writeBytes(const QByteArray& bytes) const;

        QString m_path;
        QByteArray m_originalBytes;
        QJsonObject m_root;
        bool m_originalExists = false;
        bool m_changed = false;
        bool m_committed = false;
        bool m_finalized = false;
    };

    explicit SecretStore(QString directory);

    // secretId 固定为 plugin/<plugin-id>/<field-key>，避免插件互相引用密钥。
    static QString idForPluginField(const QString& pluginId, const QString& fieldKey);
    static QJsonValue referenceValue(const QString& secretId);
    static QString referenceId(const QJsonValue& value);

    // 空值表示密钥不存在、已清空或无法解密；调用方不得把它写回普通配置。
    std::optional<QString> read(const QString& secretId) const;
    bool write(const QString& secretId, const QString& plaintext) const;
    Transaction beginTransaction() const { return Transaction(m_path); }

private:
    QString m_path;
};
