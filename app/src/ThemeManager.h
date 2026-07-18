// ThemeManager.h — 主题管理（多主题包 × 双模式）
// Q_PROPERTY: themeId（主题包 id）、mode（dark/light）、colors（当前模式色板，QML 直接消费）。
// 切换经 EventBus 发 "theme.changed"（payload: themeId/mode）。
// 设计取舍：colors 返回 QVariantMap（11 字段语义色，已 fallback 填满），QML 用
// themeManager.colors.bg 取色。每次主题变化整包 NOTIFY 触发 QML 绑定刷新，非每帧开销。
#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

class ThemeRegistry;
namespace xuanhub { class EventBus; }

class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY themeChanged)
    // currentTheme 兼容旧 QML（等同 mode），过渡期保留
    Q_PROPERTY(QString currentTheme READ mode NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)

public:
    explicit ThemeManager(xuanhub::EventBus* bus, ThemeRegistry* registry,
                          const QString& themeId, const QString& mode,
                          QObject* parent = nullptr);

    QString themeId() const { return m_themeId; }
    QString mode() const { return m_mode; }
    QVariantMap colors() const { return m_colors; }

    // 切 light/dark，保持 themeId（托盘 Toggle 用）
    Q_INVOKABLE void switchMode(const QString& mode);
    // 同时切主题包 + 模式（热重载/设置窗口用）
    Q_INVOKABLE void applyTheme(const QString& themeId, const QString& mode);
    // 兼容别名：等价 switchMode（旧调用点 TrayIcon 用）
    Q_INVOKABLE void switchTheme(const QString& mode);

signals:
    void themeChanged();

private:
    // 从 registry 解析当前 themeId+mode 的完整色板（缺失字段 fallback 到默认主题）
    QVariantMap resolveColors(const QString& themeId, const QString& mode) const;

    xuanhub::EventBus* m_bus;
    ThemeRegistry* m_registry;
    QString m_themeId;
    QString m_mode;
    QVariantMap m_colors;
};
