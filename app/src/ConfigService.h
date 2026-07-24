// ConfigService.h — 配置服务
// M2：增加 plugins 段解析（id/position/config）。
// M7：热重载——Q_PROPERTY 去 CONSTANT 加 NOTIFY；load() 可重入，解析失败保留旧值、
//     成功仅对变化项 emit；activePath() 暴露实际生效路径供 QFileSystemWatcher 监视。
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>
#include <QJsonObject>

struct PluginConfigEntry {
    QString id;             // 与插件 metadata().id 匹配
    QString position;       // "left"/"center"/"right"，空则默认
    int order = 0;          // 同 position 内排序，越小越靠左
    int width = 0;          // 插件在状态栏的宽度（像素，0=跟随内容自适应）
    QVariantMap config;     // 透传给插件的 pluginConfig
};

class ConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(int barHeight READ barHeight NOTIFY barHeightChanged)
    Q_PROPERTY(QString barEdge READ barEdge NOTIFY barEdgeChanged)
    Q_PROPERTY(qreal barOpacity READ barOpacity NOTIFY barOpacityChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeIdChanged)
    Q_PROPERTY(QString barMode READ barMode NOTIFY barModeChanged)

public:
    explicit ConfigService(QObject* parent = nullptr);

    // 解析顺序：exeDir/config.json（便携/开发优先）→ configDir/config.json；
    // 都不存在则把内置默认配置落地到 configDir 后读取。返回是否读到外部配置。
    // 可重入（M7）：解析失败时保留旧值返回 false；成功时仅对变化项 emit NOTIFY。
    bool load(const QString& exeDir, const QString& configDir);

    int barHeight() const { return m_barHeight; }      // 逻辑像素
    QString barEdge() const { return m_barEdge; }      // "top"/"bottom"
    qreal barOpacity() const { return m_barOpacity; }  // 0.0-1.0，1.0=不透明
    QString theme() const { return m_theme; }          // 主题模式 "dark"/"light"
    QString themeId() const { return m_themeId; }      // 主题包 id（如 "xuanhub-default"）
    QString barMode() const { return m_barMode; }      // 工作模式 "appbar"（占工作区）/"taskbar"（嵌入任务栏）
    int taskbarOffsetX() const { return m_taskbarOffsetX; }  // taskbar 模式下距任务栏左缘的水平偏移（逻辑像素）
    const QList<PluginConfigEntry>& pluginEntries() const { return m_pluginEntries; }

    // 实际生效的配置文件路径（exeDir 或 configDir 下；文件不存在时仍是目标路径）
    QString activePath() const { return m_activePath; }

    // M8 写回支持：load() 时留存原始 JSON（save 只改已知键，未知键如 bar.monitor 不丢）
    QJsonObject rawConfig() const { return m_raw; }
    // QSaveFile 原子写 activePath()；成功更新 m_raw。生效走 M7 文件监听，本函数不直接 emit
    bool save(const QJsonObject& root);

signals:
    void barHeightChanged();
    void barEdgeChanged();
    void barOpacityChanged();
    void themeChanged();
    void themeIdChanged();
    void barModeChanged();

private:
    int m_barHeight = 40;
    QString m_barEdge = QStringLiteral("top");
    qreal m_barOpacity = 1.0;                              // 0.0-1.0，1.0=不透明
    QString m_theme = QStringLiteral("dark");              // 主题模式（dark/light）
    QString m_themeId = QStringLiteral("xuanhub-default"); // 主题包 id
    QString m_barMode = QStringLiteral("appbar");          // 工作模式（appbar/taskbar）
    int m_taskbarOffsetX = 0;                              // taskbar 模式水平偏移（逻辑像素）
    QList<PluginConfigEntry> m_pluginEntries;
    QString m_activePath;
    QJsonObject m_raw;   // load() 时留存的原始 JSON（M8 save 的组装底稿）
};
