// PluginListModel.h — QML 可用的插件列表模型
// 角色：插件内容、布局与运行时健康状态。
// 暴露 leftPlugins / centerPlugins / rightPlugins 三个过滤子模型。
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QVector>
#include <QUrl>
#include <QPointer>
#include <QVariantMap>

#include "PluginManager.h"

class PluginFilterModel;

class PluginListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* leftPlugins   READ leftPlugins   CONSTANT)
    Q_PROPERTY(QAbstractItemModel* centerPlugins READ centerPlugins CONSTANT)
    Q_PROPERTY(QAbstractItemModel* rightPlugins  READ rightPlugins  CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        PositionRole,
        QmlUrlRole,
        ViewModelRole,
        WidthRole,
        HealthStateRole,
        LastErrorRole,
        LastErrorAtRole,
        StartedAtRole,
        LastHealthyAtRole,
        RestartCountRole,
        ConsecutiveFailuresRole,
        LoadDurationMsRole,
        QueueDepthRole,
    };

    explicit PluginListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlugins(const QVector<PluginManager::LoadedPlugin>& items);

    QAbstractItemModel* leftPlugins()   const;
    QAbstractItemModel* centerPlugins() const;
    QAbstractItemModel* rightPlugins()  const;

    // 插件槽（PluginSlot.qml）上报的自然内容宽度（逻辑像素）；
    // 设置窗口切「手动」宽度时以此作初始值。与 WidthRole（config 定宽）无关。
    Q_INVOKABLE void reportContentWidth(const QString& id, int w);
    // 插件代理或 PluginSlot 以稳定字段增量上报；未提供的指标保持原值。
    Q_INVOKABLE void reportHealth(const QString& id, const QVariantMap& update);
    int contentWidth(const QString& id) const;   // 无记录返回 0

private:
    struct Item {
        QString id;
        QString position;
        QUrl qmlUrl;
        QObject* vm = nullptr;
        int width = 0;
        xuanhub::PluginHealth health;
    };
    QVector<Item> m_items;
    QHash<QString, int> m_contentWidths;

    QPointer<PluginFilterModel> m_leftModel;
    QPointer<PluginFilterModel> m_centerModel;
    QPointer<PluginFilterModel> m_rightModel;
};
