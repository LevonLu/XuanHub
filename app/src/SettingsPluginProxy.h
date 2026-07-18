// SettingsPluginProxy.h — 插件自绘设置页（settingsQml）的 config 草稿桥（M10）
// 契约：插件设置 QML 的根 Item 声明 property var plugin，框架（设置窗口的 Loader）
// 把本对象注入；插件经 value(key)/setValue(key, v) 读写 config.json 的 config 段草稿。
// 语义铁律：自绘页只碰草稿——应用/保存由设置窗口统一落盘，取消整块丢弃，
// 热重载后才真正生效；自绘页不直接写文件。
#pragma once
#include <QObject>
#include <QString>
#include <QVariant>

class SettingsPluginModel;

class SettingsPluginProxy : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId WRITE setPluginId NOTIFY pluginIdChanged)
public:
    explicit SettingsPluginProxy(SettingsPluginModel* model, QObject* parent = nullptr);

    QString pluginId() const { return m_pluginId; }
    void setPluginId(const QString& id);

    Q_INVOKABLE QVariant value(const QString& key) const;          // 无该键 → QVariant()（QML undefined）
    Q_INVOKABLE void setValue(const QString& key, const QVariant& v);

signals:
    void pluginIdChanged();

private:
    SettingsPluginModel* m_model;
    QString m_pluginId;
};
