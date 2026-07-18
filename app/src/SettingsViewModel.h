// SettingsViewModel.h — 设置窗口后端（M8）
// 属性 = 编辑中的草稿；apply() 校验后经 ConfigService::save 写回 config.json，
// 生效由 M7 ConfigHotReloader 的文件监听自动完成（本类不直接触发 reload，
// 避免与 watcher 双重重载）。
// M10：pluginModel 改为排序 proxy（启用在前）；pluginProxy 为插件自绘设置页
//（settingsQml）的 config 草稿桥，QML 选中变化时重定向其 pluginId。
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

class QAbstractItemModel;
class ConfigService;
class ThemeRegistry;
class SettingsPluginModel;
class SettingsPluginSortModel;
class SettingsPluginProxy;

class SettingsViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int barHeight READ barHeight WRITE setBarHeight NOTIFY barHeightChanged)
    Q_PROPERTY(int barOpacity READ barOpacity WRITE setBarOpacity NOTIFY barOpacityChanged)
    Q_PROPERTY(QString barEdge READ barEdge WRITE setBarEdge NOTIFY barEdgeChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString themeId READ themeId WRITE setThemeId NOTIFY themeIdChanged)
    Q_PROPERTY(QStringList themePackNames READ themePackNames NOTIFY themePacksReloaded)
    Q_PROPERTY(QStringList themePackIds READ themePackIds NOTIFY themePacksReloaded)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(QAbstractItemModel* pluginModel READ pluginModel CONSTANT)
    Q_PROPERTY(QObject* pluginProxy READ pluginProxy CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    SettingsViewModel(ConfigService* config, ThemeRegistry* registry,
                      QString exeDir, QObject* parent = nullptr);

    int barHeight() const { return m_barHeight; }
    int barOpacity() const { return m_barOpacity; }    // 0-100 百分比（QML SpinBox 友好）
    QString barEdge() const { return m_barEdge; }
    QString theme() const { return m_theme; }          // 模式 dark/light
    QString themeId() const { return m_themeId; }      // 主题包 id
    QStringList themePackNames() const;                // 已安装主题包显示名（下拉用）
    QStringList themePackIds() const;                  // 与 names 同序的 id（保存映射用）
    bool autoStart() const { return m_autoStart; }
    QAbstractItemModel* pluginModel() const;   // 实现在 .cpp（头里模型是不完整类型）
    QObject* pluginProxy() const;              // 同上，实现在 .cpp
    QString errorMessage() const { return m_errorMessage; }

    void setBarHeight(int v);
    void setBarOpacity(int v);   // 0-100 百分比
    void setBarEdge(const QString& v);
    void setTheme(const QString& v);            // 设置模式
    void setThemeId(const QString& v);          // 设置主题包 id
    void setAutoStart(bool v);

    // 重新扫描 themes/ 刷新主题包列表（打开窗口 / 用户可能装了新包时调）
    Q_INVOKABLE void reloadThemePacks();

    // 从 ConfigService 现值 + 磁盘发现重建草稿（每次打开窗口时调用）
    Q_INVOKABLE void reloadFromConfig();
    // 校验 → 组装 JSON（保留未知键）→ ConfigService::save；失败置 errorMessage 返回 false
    Q_INVOKABLE bool apply();

signals:
    void barHeightChanged();
    void barOpacityChanged();
    void barEdgeChanged();
    void themeChanged();
    void themeIdChanged();
    void themePacksReloaded();
    void autoStartChanged();
    void errorMessageChanged();

private:
    void setErrorMessage(const QString& m);

    ConfigService* m_config;
    ThemeRegistry* m_registry;
    QString m_exeDir;
    SettingsPluginModel* m_pluginModel;
    SettingsPluginSortModel* m_sortModel;      // 排序 proxy（QML 拿到的是它）
    SettingsPluginProxy* m_pluginProxy;        // 自绘设置页的 config 草稿桥
    int m_barHeight = 40;
    int m_barOpacity = 100;                              // 0-100 百分比（100=不透明）
    QString m_barEdge = QStringLiteral("top");
    QString m_theme = QStringLiteral("dark");            // 模式
    QString m_themeId = QStringLiteral("xuanhub-default"); // 主题包 id
    bool m_autoStart = false;
    QString m_errorMessage;
};
