// ThemeManager.cpp
#include "ThemeManager.h"
#include "barplugin/EventBus.h"

ThemeManager::ThemeManager(xuanhub::EventBus* bus, const QString& initial,
                           QObject* parent)
    : QObject(parent), m_bus(bus), m_theme(initial) {}

void ThemeManager::switchTheme(const QString& theme) {
    if (m_theme == theme) return;
    m_theme = theme;
    emit themeChanged();
    if (m_bus) m_bus->publish(QStringLiteral("theme.changed"),
                              {{QStringLiteral("theme"), theme}});
}
