// XuanHubMenuViewModel.h — 快速菜单状态与宿主动作的 EventBus 代理
#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace xuanhub { class PluginEventScope; }

class XuanHubMenuViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoStartEnabled READ autoStartEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool taskbarMode READ taskbarMode NOTIFY stateChanged)
    Q_PROPERTY(QString themeMode READ themeMode NOTIFY stateChanged)
    Q_PROPERTY(bool pluginActionsEnabled READ pluginActionsEnabled NOTIFY actionFilterChanged)
    Q_PROPERTY(int actionFilterRevision READ actionFilterRevision NOTIFY actionFilterChanged)

public:
    explicit XuanHubMenuViewModel(xuanhub::PluginEventScope* eventScope,
                                  QObject* parent = nullptr);

    bool autoStartEnabled() const { return m_autoStartEnabled; }
    bool taskbarMode() const { return m_taskbarMode; }
    QString themeMode() const { return m_themeMode; }
    bool pluginActionsEnabled() const { return m_pluginActionsEnabled; }
    int actionFilterRevision() const { return m_actionFilterRevision; }

    Q_INVOKABLE void requestState();
    Q_INVOKABLE void invokeAction(const QString& action, bool enabled = false);
    Q_INVOKABLE bool isPluginActionEnabled(const QString& pluginId,
                                           const QString& actionId) const;

    void applyConfig(const QVariantMap& config);

signals:
    void stateChanged();
    void actionFilterChanged();

private slots:
    void onState(QVariantMap envelope);

private:
    xuanhub::PluginEventScope* m_eventScope;
    bool m_autoStartEnabled = false;
    bool m_taskbarMode = false;
    QString m_themeMode = QStringLiteral("dark");
    bool m_pluginActionsEnabled = true;
    int m_actionFilterRevision = 0;
    QSet<QString> m_disabledPluginActions;
};
