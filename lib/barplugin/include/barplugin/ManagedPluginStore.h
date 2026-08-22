// ManagedPluginStore.h — 宿主与 host 插件共享的受管插件当前版本只读视图
#pragma once

#include "barplugin_export.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace xuanhub {

struct ManagedPluginVersion {
    QString id;
    QString version;
    QString previousVersion;
    QString runtime;
    QString payloadDirectory;
    QJsonObject installRecord;
};

BARPLUGIN_EXPORT QString managedPluginStoreRoot(
    const QString& configDirectory);
BARPLUGIN_EXPORT ManagedPluginVersion readManagedPluginCurrent(
    const QString& configDirectory,
    const QString& id);
BARPLUGIN_EXPORT QList<ManagedPluginVersion> readManagedPluginCurrents(
    const QString& configDirectory,
    const QString& runtime = {});

} // namespace xuanhub
