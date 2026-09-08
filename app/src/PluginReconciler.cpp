// PluginReconciler.cpp — 增量停止、重启与配置确认，失败只影响依赖闭包
#include "PluginReconciler.h"
#include "PluginManager.h"
#include "PluginChangeSet.h"
#include <QMap>
#include <QSet>

bool PluginReconciler::apply(PluginManager& manager, const QString& pluginsDir,
                              const QList<PluginConfigEntry>& desired) {
    const auto before = manager.configEntries();
    const auto changes = PluginConfigDiff::calculate(before, desired);
    QMap<QString, PluginConfigEntry> oldById, nextById;
    for (const auto& entry : before) oldById.insert(entry.id, entry);
    for (const auto& entry : desired) nextById.insert(entry.id, entry);
    QSet<QString> stopIds, loadIds;
    for (const auto& id : changes.removed) stopIds.insert(id);
    const auto states = manager.configStates();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (!nextById.contains(it.key())) stopIds.insert(it.key());
    }
    for (const auto& id : changes.added) {
        loadIds.insert(id);
        if (manager.isLoaded(id)) stopIds.insert(id); // 上次异步初始化失败，替换目标实例。
    }
    bool waiting = false;
    for (const auto& id : changes.configChanged) {
        const auto& next = nextById[id];
        if (oldById[id].configVersion != next.configVersion) {
            stopIds.insert(id); // 版本变化必须重新走迁移与初始化验证。
            continue;
        }
        const auto result = manager.applyConfig(next);
        if (result == PluginManager::ConfigApplyResult::Unsupported) stopIds.insert(id);
        else if (result == PluginManager::ConfigApplyResult::Pending) {
            for (const auto& active : manager.configEntries()) {
                if (active.id == id && PluginConfigDiff::backendFingerprint(active)
                        != PluginConfigDiff::backendFingerprint(next)) waiting = true;
            }
        }
    }
    const auto roots = stopIds.values();
    for (const auto& id : roots) {
        for (const auto& dependent : manager.dependentPluginIds(id)) stopIds.insert(dependent);
    }
    auto orderedStops = stopIds.values();
    orderedStops.sort();
    for (const auto& id : orderedStops) {
        if (manager.stopOne(id)) {
            if (nextById.contains(id)) loadIds.insert(id);
        } else {
            loadIds.remove(id);
        }
    }
    // 顺序可能跨原生依赖和 hosted 子插件；每轮必须有进展才继续。
    while (!loadIds.isEmpty()) {
        bool progressed = false;
        auto ids = loadIds.values();
        ids.sort();
        for (const auto& id : ids) {
            if (manager.isLoaded(id) || manager.loadOne(pluginsDir, nextById[id])) {
                loadIds.remove(id);
                progressed = true;
            }
        }
        if (!progressed) break;
    }
    for (const auto& entry : desired) manager.updateLayout(entry);
    return waiting;
}
