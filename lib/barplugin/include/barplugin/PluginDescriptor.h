// PluginDescriptor.h — V2 Manifest 与运行时共享的统一插件描述
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace xuanhub {

struct PluginEntry {
    QString backend;
    QString qml;
    QString settingsQml;
};

struct PluginDependency {
    QString id;
    QString versionConstraint;
    bool optional = false;
};

struct PluginCompatibility {
    QString architecture;
    QString qtVersion;
    QString compiler;
    QString crt;
};

struct PluginDescriptor {
    QString id;
    QString displayName;
    QString version;
    int apiVersion = 1;
    QString minHostVersion;
    QString maxHostVersion;
    QString author;
    PluginEntry entry;
    QStringList capabilities;
    QStringList permissions;
    QVector<PluginDependency> dependencies;
    PluginCompatibility compatibility;
};

} // namespace xuanhub
