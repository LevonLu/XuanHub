// PluginConfigTracker.h — 区分已确认配置、等待回执的配置与应用失败
#pragma once
#include "ConfigService.h"
#include <QMap>

class PluginConfigTracker final {
public:
    void begin(const PluginConfigEntry& entry) {
        m_pending.insert(entry.id, entry);
        setState(entry.id, QStringLiteral("pending"));
    }
    void applied(const PluginConfigEntry& entry) {
        m_pending.remove(entry.id);
        m_applied.insert(entry.id, entry);
        setState(entry.id, QStringLiteral("applied"));
    }
    void failed(const QString& id, const QString& error) {
        m_pending.remove(id);
        setState(id, QStringLiteral("failed"), error);
    }
    void removed(const QString& id) {
        m_pending.remove(id);
        m_applied.remove(id);
        m_states.remove(id);
    }
    const PluginConfigEntry* pending(const QString& id) const {
        const auto it = m_pending.constFind(id);
        return it == m_pending.cend() ? nullptr : &it.value();
    }
    // 等待回执期间不重复排队；失败后退回上次确认值，使同一文件能再次重试。
    QList<PluginConfigEntry> entries() const {
        auto entries = m_applied;
        for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
            entries.insert(it.key(), it.value());
        return entries.values();
    }
    void layout(const PluginConfigEntry& entry) {
        const auto confirmed = m_applied.constFind(entry.id);
        if (!m_pending.contains(entry.id) && confirmed != m_applied.cend()
            && confirmed->config == entry.config && confirmed->configVersion == entry.configVersion)
            setState(entry.id, QStringLiteral("applied")); // 用户撤回失败配置。
        for (auto* entries : {&m_applied, &m_pending}) {
            auto it = entries->find(entry.id);
            if (it == entries->end()) continue;
            it->position = entry.position;
            it->order = entry.order;
            it->width = entry.width;
        }
    }
    QVariantMap states() const { return m_states; }
private:
    void setState(const QString& id, const QString& state, const QString& error = {}) {
        // 插件可能返回 DLL 内的 QStringLiteral，诊断须持有宿主自己的字符存储。
        const QString ownedError(error.constData(), error.size());
        m_states.insert(id, QVariantMap{{QStringLiteral("state"), state},
                                       {QStringLiteral("error"), ownedError}});
    }
    QMap<QString, PluginConfigEntry> m_applied;
    QMap<QString, PluginConfigEntry> m_pending;
    QVariantMap m_states;
};
