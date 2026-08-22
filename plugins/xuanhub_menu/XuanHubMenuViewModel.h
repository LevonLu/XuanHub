// XuanHubMenuViewModel.h — 快速菜单状态与宿主动作的 EventBus 代理
#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace xuanhub { class EventBus; }

class XuanHubMenuViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoStartEnabled READ autoStartEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool taskbarMode READ taskbarMode NOTIFY stateChanged)
    Q_PROPERTY(QString themeMode READ themeMode NOTIFY stateChanged)

public:
    explicit XuanHubMenuViewModel(xuanhub::EventBus* bus, QObject* parent = nullptr);

    bool autoStartEnabled() const { return m_autoStartEnabled; }
    bool taskbarMode() const { return m_taskbarMode; }
    QString themeMode() const { return m_themeMode; }

    Q_INVOKABLE void requestState();
    Q_INVOKABLE void invokeAction(const QString& action, bool enabled = false);

signals:
    void stateChanged();

private slots:
    void onState(QVariantMap payload);

private:
    xuanhub::EventBus* m_bus;
    bool m_autoStartEnabled = false;
    bool m_taskbarMode = false;
    QString m_themeMode = QStringLiteral("dark");
};
