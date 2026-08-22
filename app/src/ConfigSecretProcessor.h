// ConfigSecretProcessor.h — 按设置 Schema 保护和注入嵌套插件密钥
#pragma once

#include "SecretStore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

class ConfigSecretProcessor final {
public:
    static bool protect(const QString& pluginId, const QJsonArray& schema,
                        QVariantMap* config, const QJsonObject& rawConfig,
                        SecretStore::Transaction* transaction, QString* error);

    static void hydrate(const QString& pluginId, const QJsonArray& schema,
                        const QJsonObject& rawConfig, QVariantMap* config,
                        const SecretStore& secrets);

    // ConfigService 在插件实例化前读取 sidecar，获取与设置页相同的 settings schema。
    static QJsonArray loadSchema(const QString& exeDir, const QString& pluginId);
};
