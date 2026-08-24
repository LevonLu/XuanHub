// ThemeManager.h — 主题管理（多主题包 × 双模式 × 可选 Bar 外壳扩展）
// colors 保持旧契约；appearance/chromeUrl 提供 Theme Schema v2 与可信 QML 外壳入口。
// 当前状态保留在 EventBus "app.theme"；兼容期仍发 "theme.changed"。
// 设计取舍：colors 返回 QVariantMap（11 字段语义色，已 fallback 填满），QML 用
// themeManager.colors.bg 取色。每次主题变化整包 NOTIFY 触发 QML 绑定刷新，非每帧开销。
#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

class ThemeRegistry;
namespace xuanhub { class EventBus; }

class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap appearance READ appearance NOTIFY themeChanged)
    Q_PROPERTY(QUrl chromeUrl READ chromeUrl NOTIFY themeChanged)
    Q_PROPERTY(bool customChrome READ customChrome NOTIFY themeChanged)
    Q_PROPERTY(QStringList chromeModes READ chromeModes NOTIFY themeChanged)

public:
    explicit ThemeManager(xuanhub::EventBus* bus, ThemeRegistry* registry,
                          const QString& themeId, const QString& mode,
                          QObject* parent = nullptr);

    QString themeId() const { return m_themeId; }
    QString mode() const { return m_mode; }
    QVariantMap colors() const { return m_colors; }
    QVariantMap appearance() const { return m_appearance; }
    QUrl chromeUrl() const { return m_chromeUrl; }
    bool customChrome() const { return !m_chromeUrl.isEmpty(); }
    QStringList chromeModes() const { return m_chromeModes; }
    Q_INVOKABLE bool chromeSupportsMode(const QString& barMode) const;

    // 切 light/dark，保持 themeId（托盘 Toggle 用）
    Q_INVOKABLE void switchMode(const QString& mode);
    // 同时切主题包 + 模式（热重载/设置窗口用）
    Q_INVOKABLE void applyTheme(const QString& themeId, const QString& mode);
    // registry 重扫后重解析当前色板（themeId/mode 未变但主题包内容可能改了）；
    // 色板无变化则静默返回
    void refresh();

signals:
    void themeChanged();

private:
    // 从 registry 解析当前 themeId+mode 的完整色板（缺失字段 fallback 到默认主题）
    QVariantMap resolveColors(const QString& themeId, const QString& mode) const;
    QVariantMap resolveAppearance(const QString& themeId, const QString& mode) const;
    QUrl resolveChromeUrl(const QString& themeId) const;
    QStringList resolveChromeModes(const QString& themeId) const;
    void publishTheme();

    xuanhub::EventBus* m_bus;
    ThemeRegistry* m_registry;
    QString m_themeId;
    QString m_mode;
    QVariantMap m_colors;
    QVariantMap m_appearance;
    QUrl m_chromeUrl;
    QStringList m_chromeModes;
};
