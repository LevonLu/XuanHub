// IBarPlugin2.h — 带统一 Descriptor 和结构化结果的插件 SDK V2 接口
#pragma once

#include "barplugin/PluginDescriptor.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace xuanhub {
class EventBus;
class PluginEventScope;

struct PluginContext2 {
    EventBus* eventBus = nullptr;
    QString configDir;
    QVariantMap pluginConfig;
    PluginEventScope* eventScope = nullptr;
    QHash<QString, QString> dependencyVersions;

    bool hasDependency(const QString& id) const {
        return dependencyVersions.contains(id);
    }
};

struct PluginInitResult {
    bool success = false;
    QString error;

    static PluginInitResult succeeded() { return {true, {}}; }
    static PluginInitResult failed(const QString& error) { return {false, error}; }
};

struct PluginConfigValidationResult {
    bool valid = false;
    QString error;

    static PluginConfigValidationResult accepted() { return {true, {}}; }
    static PluginConfigValidationResult rejected(const QString& error) {
        return {false, error};
    }
};

enum class PluginConfigApplyDisposition {
    Applied,
    RequiresRestart,
    Rejected,
};

struct PluginConfigApplyResult {
    PluginConfigApplyDisposition disposition = PluginConfigApplyDisposition::Rejected;
    QString error;

    static PluginConfigApplyResult applied() {
        return {PluginConfigApplyDisposition::Applied, {}};
    }
    static PluginConfigApplyResult requiresRestart() {
        return {PluginConfigApplyDisposition::RequiresRestart, {}};
    }
    static PluginConfigApplyResult rejected(const QString& error) {
        return {PluginConfigApplyDisposition::Rejected, error};
    }
};

class IBarPlugin2 {
public:
    virtual ~IBarPlugin2() = default;

    virtual PluginDescriptor descriptor() const = 0;
    virtual PluginInitResult initialize(const PluginContext2& context) = 0;
    virtual PluginConfigValidationResult validateConfig(
        const QVariantMap& config) const = 0;
    virtual PluginConfigApplyResult applyConfig(const QVariantMap& config) = 0;
    virtual QObject* viewModel() = 0;
    virtual QUrl qmlUrl() const = 0;
    virtual void shutdown() = 0;
};

} // namespace xuanhub

Q_DECLARE_INTERFACE(xuanhub::IBarPlugin2, "xuanhub.BarPlugin/2.0")
