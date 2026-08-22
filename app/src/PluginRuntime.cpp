// PluginRuntime.cpp — V1 兼容适配与 V2 结构化生命周期实现
#include "PluginRuntime.h"

#include "barplugin/BarPluginContext.h"
#include "barplugin/IBarPlugin.h"
#include "barplugin/IBarPlugin2.h"
#include "barplugin/IBarPluginConfigurable.h"
#include "barplugin/IPluginConfigMigration.h"

namespace {
void setError(QString* error, const QString& text) {
    if (error) *error = text;
}

bool sameDependency(const xuanhub::PluginDependency& left,
                    const xuanhub::PluginDependency& right) {
    return left.id == right.id
        && left.versionConstraint == right.versionConstraint
        && left.optional == right.optional;
}

bool sameV2Descriptor(const xuanhub::PluginDescriptor& left,
                      const xuanhub::PluginDescriptor& right) {
    if (left.id != right.id || left.displayName != right.displayName
        || left.version != right.version || left.apiVersion != right.apiVersion
        || left.configVersion != right.configVersion
        || left.minHostVersion != right.minHostVersion
        || left.maxHostVersion != right.maxHostVersion
        || left.author != right.author
        || left.entry.backend != right.entry.backend
        || left.entry.qml != right.entry.qml
        || left.entry.settingsQml != right.entry.settingsQml
        || left.capabilities != right.capabilities
        || left.permissions != right.permissions
        || left.compatibility.architecture != right.compatibility.architecture
        || left.compatibility.qtVersion != right.compatibility.qtVersion
        || left.compatibility.compiler != right.compatibility.compiler
        || left.compatibility.crt != right.compatibility.crt
        || left.dependencies.size() != right.dependencies.size()) {
        return false;
    }
    for (qsizetype i = 0; i < left.dependencies.size(); ++i) {
        if (!sameDependency(left.dependencies.at(i), right.dependencies.at(i))) return false;
    }
    return true;
}
} // namespace

PluginRuntime::PluginRuntime(QObject* instance, xuanhub::IBarPlugin* v1,
                             xuanhub::IBarPlugin2* v2)
    : m_instance(instance), m_v1(v1), m_v2(v2) {}

std::unique_ptr<PluginRuntime> PluginRuntime::create(
    QObject* instance, const QString& iid, QString* error) {
    if (!instance) {
        setError(error, QStringLiteral("plugin instance is null"));
        return {};
    }
    if (iid == QLatin1String("xuanhub.BarPlugin/2.0")) {
        auto* v2 = qobject_cast<xuanhub::IBarPlugin2*>(instance);
        if (!v2) {
            setError(error, QStringLiteral("qobject_cast<IBarPlugin2*> failed"));
            return {};
        }
        return std::unique_ptr<PluginRuntime>(new PluginRuntime(instance, nullptr, v2));
    }
    if (iid == QLatin1String("xuanhub.BarPlugin/1.0")) {
        auto* v1 = qobject_cast<xuanhub::IBarPlugin*>(instance);
        if (!v1) {
            setError(error, QStringLiteral("qobject_cast<IBarPlugin*> failed"));
            return {};
        }
        return std::unique_ptr<PluginRuntime>(new PluginRuntime(instance, v1, nullptr));
    }
    setError(error, QStringLiteral("unsupported plugin IID: %1").arg(iid));
    return {};
}

xuanhub::PluginDescriptor PluginRuntime::descriptor() const {
    if (m_v2) return m_v2->descriptor();
    const xuanhub::PluginMetadata metadata = m_v1->metadata();
    xuanhub::PluginDescriptor descriptor;
    descriptor.id = metadata.id;
    descriptor.displayName = metadata.displayName;
    descriptor.version = metadata.version;
    descriptor.author = metadata.author;
    descriptor.apiVersion = 1;
    return descriptor;
}

bool PluginRuntime::descriptorMatches(const xuanhub::PluginDescriptor& manifest,
                                      QString* error) const {
    const xuanhub::PluginDescriptor runtime = descriptor();
    const bool matches = isV2()
        ? sameV2Descriptor(runtime, manifest)
        : runtime.id == manifest.id && runtime.version == manifest.version;
    if (!matches) {
        setError(error, QStringLiteral("sidecar and runtime descriptor differ"));
        return false;
    }
    if (error) error->clear();
    return true;
}

bool PluginRuntime::initialize(xuanhub::BarPluginContext* context, QString* error) {
    if (m_v1) {
        const bool initialized = m_v1->init(context);
        if (!initialized) setError(error, QStringLiteral("V1 init returned false"));
        return initialized;
    }
    xuanhub::PluginContext2 context2;
    if (context) {
        context2.eventBus = context->eventBus;
        context2.configDir = context->configDir;
        context2.pluginConfig = context->pluginConfig;
        context2.eventScope = context->eventScope;
        context2.dependencyVersions = context->dependencyVersions;
        context2.hostServices = context->hostServices;
    }
    const xuanhub::PluginInitResult result = m_v2->initialize(context2);
    if (!result.success) setError(error, result.error);
    return result.success;
}

QObject* PluginRuntime::viewModel() const {
    return m_v2 ? m_v2->viewModel() : m_v1->viewModel();
}

QUrl PluginRuntime::qmlUrl() const {
    return m_v2 ? m_v2->qmlUrl() : m_v1->qmlUrl();
}

PluginRuntime::ValidationResult PluginRuntime::validateConfig(
    const QVariantMap& config) const {
    if (m_v2) {
        const auto validation = m_v2->validateConfig(config);
        return validation.valid
            ? ValidationResult{ValidationStatus::Valid, {}}
            : ValidationResult{ValidationStatus::Rejected, validation.error};
    }
    auto* configurable = qobject_cast<xuanhub::IBarPluginConfigurable*>(m_instance);
    if (!configurable) return {};
    const QString error = configurable->validateConfig(config);
    return error.isEmpty()
        ? ValidationResult{ValidationStatus::Valid, {}}
        : ValidationResult{ValidationStatus::Rejected, error};
}

PluginRuntime::ApplyResult PluginRuntime::applyConfig(const QVariantMap& config) {
    const ValidationResult validation = validateConfig(config);
    if (validation.status == ValidationStatus::Rejected)
        return {ApplyStatus::Rejected, validation.error};
    if (m_v2) {
        const auto result = m_v2->applyConfig(config);
        switch (result.disposition) {
        case xuanhub::PluginConfigApplyDisposition::Applied:
            return {ApplyStatus::Applied, {}};
        case xuanhub::PluginConfigApplyDisposition::RequiresRestart:
            return {ApplyStatus::RequiresRestart, {}};
        case xuanhub::PluginConfigApplyDisposition::Rejected:
            return {ApplyStatus::Rejected, result.error};
        }
    }
    auto* configurable = qobject_cast<xuanhub::IBarPluginConfigurable*>(m_instance);
    if (!configurable) return {};
    if (!configurable->applyConfig(config))
        return {ApplyStatus::Rejected, QStringLiteral("V1 applyConfig returned false")};
    return {ApplyStatus::Applied, {}};
}

PluginRuntime::MigrationResult PluginRuntime::migrateConfig(
    int currentVersion, const QVariantMap& config) const {
    const int targetVersion = qMax(1, descriptor().configVersion);
    if (currentVersion == targetVersion)
        return {MigrationStatus::NotRequired, targetVersion, config, {}};
    if (currentVersion > targetVersion) {
        return {MigrationStatus::Rejected, targetVersion, {},
                QStringLiteral("config version %1 is newer than plugin target %2")
                    .arg(currentVersion).arg(targetVersion)};
    }

    auto* migration = qobject_cast<xuanhub::IPluginConfigMigration*>(m_instance);
    if (!migration) {
        return {MigrationStatus::Unsupported, targetVersion, {},
                QStringLiteral("plugin does not provide a config migration")};
    }
    const xuanhub::PluginConfigMigrationResult result =
        migration->migrateConfig(currentVersion, config);
    if (!result.success) {
        return {MigrationStatus::Rejected, targetVersion, {},
                result.error.isEmpty()
                    ? QStringLiteral("plugin config migration failed") : result.error};
    }
    if (m_v2) {
        const auto validation = m_v2->validateConfig(result.config);
        if (!validation.valid) {
            return {MigrationStatus::Rejected, targetVersion, {},
                    validation.error.isEmpty()
                        ? QStringLiteral("migrated config is invalid") : validation.error};
        }
    }
    return {MigrationStatus::Migrated, targetVersion, result.config, {}};
}

void PluginRuntime::shutdown() {
    if (m_v2) m_v2->shutdown();
    else m_v1->shutdown();
}
