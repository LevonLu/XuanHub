// PluginDependencyGraph.cpp — 依赖版本预检、循环检测和稳定拓扑排序
#include "PluginDependencyGraph.h"

#include "SemanticVersion.h"

#include <QSet>

#include <algorithm>
#include <functional>

PluginDependencyPlan PluginDependencyGraph::build(
    const QVector<xuanhub::PluginDescriptor>& descriptors) {
    PluginDependencyPlan plan;
    QHash<QString, int> indexById;
    QSet<QString> duplicates;
    for (int i = 0; i < descriptors.size(); ++i) {
        const QString id = descriptors.at(i).id;
        if (indexById.contains(id)) duplicates.insert(id);
        else indexById.insert(id, i);
    }
    for (const QString& id : duplicates)
        plan.errors.insert(id, QStringLiteral("duplicate plugin id: %1").arg(id));

    // 第一轮只校验清单中直接可判断的缺失和版本约束；可选依赖不阻断加载。
    for (const auto& descriptor : descriptors) {
        if (plan.errors.contains(descriptor.id)) continue;
        for (const auto& dependency : descriptor.dependencies) {
            if (duplicates.contains(dependency.id)) {
                if (!dependency.optional) {
                    plan.errors.insert(descriptor.id,
                        QStringLiteral("required dependency '%1' is duplicated")
                            .arg(dependency.id));
                }
                continue;
            }
            const auto dependencyIt = indexById.constFind(dependency.id);
            if (dependencyIt == indexById.cend()) {
                if (!dependency.optional) {
                    plan.errors.insert(descriptor.id,
                        QStringLiteral("missing required dependency: %1")
                            .arg(dependency.id));
                }
                continue;
            }
            const QString actualVersion = descriptors.at(dependencyIt.value()).version;
            QString constraintError;
            const bool matches = SemanticVersion::matches(
                actualVersion, dependency.versionConstraint, &constraintError);
            if (!matches && !dependency.optional) {
                plan.errors.insert(descriptor.id,
                    QStringLiteral("dependency '%1' version %2 does not match %3")
                        .arg(dependency.id, actualVersion,
                             dependency.versionConstraint));
            }
        }
    }

    // 必需依赖环是不可恢复错误。DFS 只标记实际环成员，随后再传播到依赖它们的插件。
    QHash<QString, int> state;
    QStringList stack;
    std::function<void(const QString&)> visit = [&](const QString& id) {
        if (plan.errors.contains(id) || state.value(id) == 2) return;
        if (state.value(id) == 1) {
            const qsizetype start = stack.indexOf(id);
            for (qsizetype i = start; i < stack.size(); ++i)
                plan.errors.insert(stack.at(i),
                    QStringLiteral("required dependency cycle contains: %1")
                        .arg(stack.mid(start).join(QStringLiteral(" -> "))));
            return;
        }
        state.insert(id, 1);
        stack.append(id);
        const auto& descriptor = descriptors.at(indexById.value(id));
        for (const auto& dependency : descriptor.dependencies) {
            if (dependency.optional || !indexById.contains(dependency.id)
                || duplicates.contains(dependency.id)) {
                continue;
            }
            visit(dependency.id);
        }
        stack.removeLast();
        state.insert(id, 2);
    };
    QStringList ids = indexById.keys();
    ids.sort();
    for (const QString& id : ids) visit(id);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& descriptor : descriptors) {
            if (plan.errors.contains(descriptor.id)) continue;
            for (const auto& dependency : descriptor.dependencies) {
                if (!dependency.optional && plan.errors.contains(dependency.id)) {
                    plan.errors.insert(descriptor.id,
                        QStringLiteral("required dependency '%1' is unavailable")
                            .arg(dependency.id));
                    changed = true;
                    break;
                }
            }
        }
    }

    // Kahn 排序只把必需依赖作为硬边；同层按 ID 排序，保证扫描目录顺序不影响结果。
    QSet<QString> remaining;
    for (const QString& id : ids) {
        if (!plan.errors.contains(id)) remaining.insert(id);
    }
    while (!remaining.isEmpty()) {
        QStringList ready;
        for (const QString& id : remaining) {
            bool dependenciesReady = true;
            const auto& descriptor = descriptors.at(indexById.value(id));
            for (const auto& dependency : descriptor.dependencies) {
                if (!dependency.optional && remaining.contains(dependency.id)) {
                    dependenciesReady = false;
                    break;
                }
            }
            if (dependenciesReady) ready.append(id);
        }
        ready.sort();
        if (ready.isEmpty()) break; // 理论上已由 DFS 标记，防御异常输入。
        for (const QString& id : ready) {
            plan.loadOrder.append(id);
            remaining.remove(id);
        }
    }
    return plan;
}
