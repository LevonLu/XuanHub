// ThemeRegistry.h — 主题包与 Bar 外壳扩展注册表
// 扫描 themes/*/theme.json，解析 + 校验，缓存色板、外观令牌与可选的可信 QML 外壳。
// 普通主题仍是纯 JSON；声明 barChrome 的主题扩展会执行本地 QML，选择该主题即表示信任。
// 兜底：内置硬编码默认主题（themes/xuanhub-default 丢失时也保证可用）。
#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

class ThemeRegistry : public QObject {
    Q_OBJECT
public:
    struct ThemePack {
        QString id;          // 全局唯一，config.themeId 引用
        QString name;        // 显示名（设置窗口列出）
        QString version;
        QString author;
        QVariantMap dark;    // 深色模式色板（字段集见 kColorFields）
        QVariantMap light;   // 浅色模式色板
        QVariantMap appearance;       // 两种模式共用的 Theme Schema v2 外观令牌
        QVariantMap darkAppearance;   // 深色模式覆盖
        QVariantMap lightAppearance;  // 浅色模式覆盖
        QUrl chromeUrl;                // 可选的可信 Bar 外壳 QML
        QStringList chromeModes;       // appbar/taskbar；空表示两者都支持
    };

    explicit ThemeRegistry(QObject* parent = nullptr);

    // 扫描 themesDir 下的子目录，每个含 theme.json 的解析为一个 ThemePack。
    // 解析失败/校验不过的跳过并记日志；重复 id 只保留首个。
    void reload(const QString& themesDir);

    const QVector<ThemePack>& packs() const { return m_packs; }
    const ThemePack* find(const QString& id) const;

    // 内置默认主题（保证存在，themeId 找不到时兜底）
    const ThemePack& defaultPack() const;

    // 已安装主题包的 name 列表（设置窗口 ComboBox 用，与 packs() 同序）
    QStringList packNames() const;

private:
    QVector<ThemePack> m_packs;
    ThemePack m_builtinDefault;   // 硬编码兜底（见 cpp kDefault*）
};
