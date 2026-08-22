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

struct PluginAction {
    QString id;
    QString label;
    QStringList placements;
    QString icon;
    QString defaultShortcut;
    bool checkable = false;
};

struct PluginLayoutPreference {
    bool declared = false;
    QString region;
    int minWidth = 0;
    int preferredWidth = 0;
    int maxWidth = 0; // 0 表示不另设上限
    int flex = 0;
    int priority = 50;
    QString overflow = QStringLiteral("clip");
};

struct PluginDescriptor {
    QString id;
    QString displayName;
    QString version;
    int apiVersion = 1;
    int configVersion = 1;
    QString minHostVersion;
    QString maxHostVersion;
    QString author;
    PluginEntry entry;
    QStringList capabilities;
    QStringList permissions;
    QVector<PluginAction> actions;
    PluginLayoutPreference layout;
    QVector<PluginDependency> dependencies;
    PluginCompatibility compatibility;
};

} // namespace xuanhub
