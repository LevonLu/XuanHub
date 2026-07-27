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
    // 草稿修订号：setValue 后递增。value() 是 Q_INVOKABLE、无绑定通知能力，
    // 自绘页的预览类绑定（倒计时预览、不透明度百分比等）在表达式里引用一次
    // plugin.revision 建立依赖，即可随 setValue 实时刷新
    Q_PROPERTY(int revision READ revision NOTIFY valuesChanged)
public:
    explicit SettingsPluginProxy(SettingsPluginModel* model, QObject* parent = nullptr);

    QString pluginId() const { return m_pluginId; }
    void setPluginId(const QString& id);

    int revision() const { return m_revision; }

    Q_INVOKABLE QVariant value(const QString& key) const;          // 无该键 → QVariant()（QML undefined）
    Q_INVOKABLE void setValue(const QString& key, const QVariant& v);

signals:
    void pluginIdChanged();
    void valuesChanged();

private:
    SettingsPluginModel* m_model;
    QString m_pluginId;
    int m_revision = 0;
};
