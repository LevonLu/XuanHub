// PluginHostServices.h — 绑定插件身份、权限与私有命名空间的宿主服务实现
#pragma once

#include "barplugin/HostServices.h"

#include <QString>

#include <functional>
#include <memory>

namespace xuanhub { class EventBus; }
class SecretStore;

class PluginHostServices final : public xuanhub::IHostServices,
                                 public xuanhub::IPluginLogger,
                                 public xuanhub::IPluginNotifications,
                                 public xuanhub::IPluginStorage,
                                 public xuanhub::IPluginSecrets,
                                 public xuanhub::IPluginNavigation,
                                 public xuanhub::IPluginShell,
                                 public xuanhub::IPluginLocale,
                                 public xuanhub::IPluginTheme {
public:
    struct Environment {
        xuanhub::EventBus* eventBus = nullptr;
        QString configDir;
        std::function<QString()> activeLocale;
        std::function<QVariantMap()> currentPalette;
    };

    PluginHostServices(QString pluginId,
                       xuanhub::HostPluginTrustLevel trustLevel,
                       QStringList declaredPermissions,
                       QStringList grantedPermissions,
                       Environment environment);
    ~PluginHostServices() override;

    int apiVersion() const override;
    QString pluginId() const override;
    xuanhub::HostPluginTrustLevel trustLevel() const override;
    QStringList declaredPermissions() const override;
    QStringList grantedPermissions() const override;
    QStringList capabilities() const override;
    bool hasCapability(const QString& capability) const override;
    bool hasPermission(const QString& permission) const override;

    xuanhub::IPluginLogger* logger() override;
    xuanhub::IPluginNotifications* notifications() override;
    xuanhub::IPluginStorage* storage() override;
    xuanhub::IPluginSecrets* secrets() override;
    xuanhub::IPluginNavigation* navigation() override;
    xuanhub::IPluginShell* shell() override;
    xuanhub::IPluginLocale* locale() override;
    xuanhub::IPluginTheme* theme() override;

    void log(xuanhub::PluginLogLevel level, const QString& message,
             const QVariantMap& fields = {}) override;
    xuanhub::HostServiceResult show(const QString& title,
                                     const QString& message) override;
    xuanhub::HostServiceResult readBytes(const QString& relativePath) const override;
    xuanhub::HostServiceResult writeBytes(const QString& relativePath,
                                           const QByteArray& data) override;
    xuanhub::HostServiceResult removePath(const QString& relativePath) override;
    xuanhub::HostServiceResult list(const QString& relativeDirectory = {}) const override;
    xuanhub::HostServiceResult readSecret(const QString& key) const override;
    xuanhub::HostServiceResult writeSecret(const QString& key,
                                            const QString& value) override;
    xuanhub::HostServiceResult removeSecret(const QString& key) override;
    xuanhub::HostServiceResult openSettings() override;
    xuanhub::HostServiceResult openUrl(const QString& url) override;
    xuanhub::HostServiceResult openFile(const QString& relativePath) override;
    QString activeLocale() const override;
    QVariantMap currentPalette() const override;

private:
    xuanhub::HostServiceResult requirePermission(const QString& permission) const;
    xuanhub::HostServiceResult resolveStoragePath(const QString& relativePath,
                                                   bool allowDirectory,
                                                   QString* resolved) const;
    xuanhub::HostServiceResult validateSecretKey(const QString& key) const;
    QString secretId(const QString& key) const;

    QString m_pluginId;
    xuanhub::HostPluginTrustLevel m_trustLevel;
    QStringList m_declaredPermissions;
    QStringList m_grantedPermissions;
    Environment m_environment;
    QString m_storageRoot;
    std::unique_ptr<SecretStore> m_secretStore;
};
