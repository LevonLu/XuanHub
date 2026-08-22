// HostServices.h — SDK V2 的版本化宿主服务与权限边界
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace xuanhub {

enum class HostPluginTrustLevel {
    TrustedInProcess,
    Isolated,
};

enum class PluginLogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

struct HostServiceResult {
    bool success = false;
    QVariant value;
    QString errorCode;
    QString errorMessage;

    static HostServiceResult succeeded(const QVariant& value = {}) {
        return {true, value, {}, {}};
    }
    static HostServiceResult failed(const QString& code, const QString& message) {
        return {false, {}, code, message};
    }
};

class IPluginLogger {
public:
    virtual ~IPluginLogger() = default;
    virtual void log(PluginLogLevel level, const QString& message,
                     const QVariantMap& fields = {}) = 0;
};

class IPluginNotifications {
public:
    virtual ~IPluginNotifications() = default;
    virtual HostServiceResult show(const QString& title, const QString& message) = 0;
};

class IPluginStorage {
public:
    virtual ~IPluginStorage() = default;
    virtual HostServiceResult readBytes(const QString& relativePath) const = 0;
    virtual HostServiceResult writeBytes(const QString& relativePath,
                                         const QByteArray& data) = 0;
    virtual HostServiceResult removePath(const QString& relativePath) = 0;
    virtual HostServiceResult list(const QString& relativeDirectory = {}) const = 0;
};

class IPluginSecrets {
public:
    virtual ~IPluginSecrets() = default;
    virtual HostServiceResult readSecret(const QString& key) const = 0;
    virtual HostServiceResult writeSecret(const QString& key, const QString& value) = 0;
    virtual HostServiceResult removeSecret(const QString& key) = 0;
};

class IPluginNavigation {
public:
    virtual ~IPluginNavigation() = default;
    virtual HostServiceResult openSettings() = 0;
};

class IPluginShell {
public:
    virtual ~IPluginShell() = default;
    virtual HostServiceResult openUrl(const QString& url) = 0;
    virtual HostServiceResult openFile(const QString& relativePath) = 0;
};

class IPluginLocale {
public:
    virtual ~IPluginLocale() = default;
    virtual QString activeLocale() const = 0;
};

class IPluginTheme {
public:
    virtual ~IPluginTheme() = default;
    virtual QVariantMap currentPalette() const = 0;
};

class IHostServices {
public:
    virtual ~IHostServices() = default;

    virtual int apiVersion() const = 0;
    virtual QString pluginId() const = 0;
    virtual HostPluginTrustLevel trustLevel() const = 0;
    virtual QStringList declaredPermissions() const = 0;
    virtual QStringList grantedPermissions() const = 0;
    virtual QStringList capabilities() const = 0;
    virtual bool hasCapability(const QString& capability) const = 0;
    virtual bool hasPermission(const QString& permission) const = 0;

    virtual IPluginLogger* logger() = 0;
    virtual IPluginNotifications* notifications() = 0;
    virtual IPluginStorage* storage() = 0;
    virtual IPluginSecrets* secrets() = 0;
    virtual IPluginNavigation* navigation() = 0;
    virtual IPluginShell* shell() = 0;
    virtual IPluginLocale* locale() = 0;
    virtual IPluginTheme* theme() = 0;
};

inline constexpr int kHostServicesApiVersion = 1;

// 隔离运行时只能通过宿主显式提供的权限访问受控能力。此清单同时供加载器、
// 设置界面和安装器复用，避免“界面显示已授权、运行时却拒绝”的口径漂移。
inline const QStringList& supportedIsolatedHostPermissions() {
    static const QStringList permissions{
        QStringLiteral("notifications"),
        QStringLiteral("storage.private"),
        QStringLiteral("secrets.own"),
        QStringLiteral("shell.openUrl"),
        QStringLiteral("shell.openFile"),
    };
    return permissions;
}

inline QStringList grantedHostPermissions(HostPluginTrustLevel trustLevel,
                                          const QStringList& declaredPermissions) {
    if (trustLevel == HostPluginTrustLevel::TrustedInProcess)
        return declaredPermissions;

    QStringList granted;
    const QStringList& supported = supportedIsolatedHostPermissions();
    for (const QString& permission : declaredPermissions) {
        if (supported.contains(permission) && !granted.contains(permission))
            granted.append(permission);
    }
    return granted;
}

} // namespace xuanhub
