// ConfigService.h — 配置服务
// M2：增加 plugins 段解析（id/position/config）。M3 扩展写回与热重载 hook。
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QVariantMap>

struct PluginConfigEntry {
    QString id;             // 与插件 metadata().id 匹配
    QString position;       // "left"/"center"/"right"，空则默认
    int order = 0;          // 同 position 内排序，越小越靠左
    QVariantMap config;     // 透传给插件的 pluginConfig
};

class ConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(int barHeight READ barHeight CONSTANT)
    Q_PROPERTY(QString barEdge READ barEdge CONSTANT)
    Q_PROPERTY(QString theme READ theme CONSTANT)

public:
    explicit ConfigService(QObject* parent = nullptr);

    // 优先 exe 同目录 config.json，否则用内置默认。返回是否读到外部配置。
    bool load(const QString& exeDir);

    int barHeight() const { return m_barHeight; }      // 逻辑像素
    QString barEdge() const { return m_barEdge; }      // "top"/"bottom"
    QString theme() const { return m_theme; }          // "dark"/"light"
    const QList<PluginConfigEntry>& pluginEntries() const { return m_pluginEntries; }

private:
    int m_barHeight = 40;
    QString m_barEdge = QStringLiteral("top");
    QString m_theme = QStringLiteral("dark");
    QList<PluginConfigEntry> m_pluginEntries;
};
