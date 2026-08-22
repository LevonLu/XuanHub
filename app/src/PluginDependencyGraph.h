// PluginDependencyGraph.h — 根据 Descriptor 生成可加载顺序与确定错误
#pragma once

#include "barplugin/PluginDescriptor.h"

#include <QHash>
#include <QStringList>
#include <QVector>

struct PluginDependencyPlan {
    QStringList loadOrder;
    QHash<QString, QString> errors;
};

class PluginDependencyGraph final {
public:
    static PluginDependencyPlan build(
        const QVector<xuanhub::PluginDescriptor>& descriptors);
};
