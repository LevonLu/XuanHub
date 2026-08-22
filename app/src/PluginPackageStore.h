// PluginPackageStore.h — 管理插件包的版本化存储、原子切换、回滚与卸载事务
#pragma once

#include "PluginPackageValidator.h"
#include "barplugin/ManagedPluginStore.h"

#include <QJsonObject>
#include <QStringList>

#include <functional>

struct PluginPackageStoreResult {
    bool ok = false;
    bool rolledBack = false;
    bool cleanupPending = false;
    QString error;
    QString id;
    QString version;
    QString previousVersion;
    QString payloadDirectory;
};

using ManagedPluginVersion = xuanhub::ManagedPluginVersion;

class PluginPackageStore final {
public:
    enum class FaultPoint {
        AfterStaging,
        AfterStop,
        AfterSwitch,
    };

    struct ActivationHooks {
        std::function<bool(const QString&)> isActive;
        std::function<bool(const QString&, QString*)> stop;
        std::function<bool(const QString&, QString*)> start;
    };

    using FaultInjector = std::function<bool(FaultPoint)>;

    explicit PluginPackageStore(QString configDirectory);

    static QString storeRootForConfigDirectory(const QString& configDirectory);
    QString storeRoot() const;
    QString dataRoot() const;

    PluginPackageStoreResult install(
        const QString& packagePath,
        const ActivationHooks& hooks = {},
        const FaultInjector& faultInjector = {},
        const PluginValidationEnvironment& environment =
            PluginValidationEnvironment::current());
    PluginPackageStoreResult rollback(
        const QString& id,
        const ActivationHooks& hooks = {});
    PluginPackageStoreResult uninstall(
        const QString& id,
        bool deleteUserData,
        const ActivationHooks& hooks = {});

    ManagedPluginVersion current(const QString& id) const;
    QList<ManagedPluginVersion> currentVersions(
        const QString& runtime = {}) const;

private:
    QString pluginDirectory(const QString& id) const;
    QString versionDirectory(const QString& id, const QString& version) const;
    QString currentFile(const QString& id) const;
    bool writeCurrent(const QString& id, const QString& version,
                      const QString& previousVersion, QString* error) const;
    bool removeCurrent(const QString& id, QString* error) const;
    bool copyPayload(const PluginPackageValidationResult& package,
                     const QString& destination, QString* error) const;
    void cleanupTransientDirectories() const;

    QString m_configDirectory;
    QString m_storeRoot;
    QString m_dataRoot;
};
