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
    // 反向依赖闭包，使用者先于提供者；host 子实例也按同一规则停止。
    static QStringList stopOrder(
        const QVector<xuanhub::PluginDescriptor>& descriptors,
        const QHash<QString, QString>& hostedBy,
        const QStringList& roots);
    static PluginDependencyPlan build(
        const QVector<xuanhub::PluginDescriptor>& descriptors);
};
