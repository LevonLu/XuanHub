// PluginChangeSet.h — 插件配置稳定指纹与按 ID 差异分类
#pragma once

#include "ConfigService.h"

#include <QList>
#include <QString>
#include <QStringList>

struct PluginChangeSet {
    QStringList added;
    QStringList removed;
    QStringList layoutChanged;
    QStringList configChanged;
    QStringList unchanged;

    bool isEmpty() const {
        return added.isEmpty() && removed.isEmpty()
            && layoutChanged.isEmpty() && configChanged.isEmpty();
    }
};

namespace PluginConfigDiff {

// 指纹只包含会影响插件后端实例的 config；布局字段单独计算，避免移动或改宽度重启插件。
QString backendFingerprint(const PluginConfigEntry& entry);
QString layoutFingerprint(const PluginConfigEntry& entry);

// 整段指纹按 id 排序，不受 JSON 数组顺序或 QVariantMap 键插入顺序影响。
QString entriesFingerprint(const QList<PluginConfigEntry>& entries);
PluginChangeSet calculate(const QList<PluginConfigEntry>& before,
                          const QList<PluginConfigEntry>& after);

} // namespace PluginConfigDiff
