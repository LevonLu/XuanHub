// ThemeManager.cpp
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "barplugin/EventBus.h"

#include <QDebug>

ThemeManager::ThemeManager(xuanhub::EventBus* bus, ThemeRegistry* registry,
                           const QString& themeId, const QString& mode,
                           QObject* parent)
    : QObject(parent), m_bus(bus), m_registry(registry),
      m_themeId(themeId), m_mode(mode) {
    m_colors = resolveColors(m_themeId, m_mode);
    publishTheme();
}

QVariantMap ThemeManager::resolveColors(const QString& themeId, const QString& mode) const {
    if (!m_registry) return {};
    // 找主题包；不存在则回落默认主题
    const ThemeRegistry::ThemePack* pack = m_registry->find(themeId);
    if (!pack) {
        if (!themeId.isEmpty()) {
            qWarning() << "[Theme] pack" << themeId << "not found -> fallback to default";
        }
        pack = &m_registry->defaultPack();
    }
    const QVariantMap& primary = (mode == QStringLiteral("light")) ? pack->light : pack->dark;
    const ThemeRegistry::ThemePack& def = m_registry->defaultPack();
    const QVariantMap& defMode = (mode == QStringLiteral("light")) ? def.light : def.dark;

    // 逐字段 fallback：当前主题缺失的字段用默认主题同名字段补
    QVariantMap out = primary;
    static const QStringList kAllFields = {
        QStringLiteral("bg"), QStringLiteral("panel"), QStringLiteral("fg"),
        QStringLiteral("muted"), QStringLiteral("accent"), QStringLiteral("danger"),
        QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("hover"),
        QStringLiteral("border"), QStringLiteral("onAccent")
    };
    for (const QString& f : kAllFields) {
        if (!out.contains(f)) {
            if (defMode.contains(f)) {
                out.insert(f, defMode.value(f));
                qDebug() << "[Theme] field" << f << "missing in" << pack->id << mode
                         << "-> fallback to default";
            }
        }
    }
    return out;
}

void ThemeManager::switchMode(const QString& mode) {
    applyTheme(m_themeId, mode);
}

void ThemeManager::applyTheme(const QString& themeId, const QString& mode) {
    const QString normMode = (mode == QStringLiteral("light")) ? QStringLiteral("light")
                                                                : QStringLiteral("dark");
    if (m_themeId == themeId && m_mode == normMode) return;
    m_themeId = themeId;
    m_mode = normMode;
    m_colors = resolveColors(m_themeId, m_mode);
    qDebug() << "[Theme] applied" << m_themeId << m_mode;
    emit themeChanged();
    publishTheme();
}

void ThemeManager::refresh() {
    // 主题包内容（而非 themeId/mode）可能已变化：重新解析色板，
    // 有变化才通知（QML 绑定刷新 + EventBus），避免无谓刷新
    const QVariantMap c = resolveColors(m_themeId, m_mode);
    if (c == m_colors) return;
    m_colors = c;
    qDebug() << "[Theme] refreshed" << m_themeId << m_mode;
    emit themeChanged();
    publishTheme();
}

void ThemeManager::publishTheme() {
    if (!m_bus) return;
    const QVariantMap state{
        {QStringLiteral("themeId"), m_themeId},
        {QStringLiteral("mode"), m_mode},
        {QStringLiteral("colors"), m_colors},
    };
    m_bus->setState(QStringLiteral("app.theme"), state);
    // V1 兼容广播保留一个周期；新代码订阅 retained 的 app.theme。
    m_bus->publish(QStringLiteral("theme.changed"), state);
}
