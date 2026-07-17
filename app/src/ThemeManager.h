// ThemeManager.h — 主题管理（dark/light）
// Q_PROPERTY currentTheme 供 QML 绑定；switchTheme 通过 EventBus 发 "theme.changed"。
#pragma once
#include <QObject>
#include <QString>

namespace xuanhub { class EventBus; }

class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY themeChanged)

public:
    explicit ThemeManager(xuanhub::EventBus* bus, const QString& initial,
                          QObject* parent = nullptr);

    QString currentTheme() const { return m_theme; }

    Q_INVOKABLE void switchTheme(const QString& theme);

signals:
    void themeChanged();

private:
    xuanhub::EventBus* m_bus;
    QString m_theme;
};
