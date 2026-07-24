// PluginListModel.h — QML 可用的插件列表模型
// 角色：id / position / qmlUrl / viewModel。
// 暴露 leftPlugins / centerPlugins / rightPlugins 三个过滤子模型。
#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QUrl>
#include <QPointer>

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
        WidthRole
    };

    explicit PluginListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlugins(const QVector<PluginManager::LoadedPlugin>& items);

    QAbstractItemModel* leftPlugins()   const;
    QAbstractItemModel* centerPlugins() const;
    QAbstractItemModel* rightPlugins()  const;

private:
    struct Item { QString id; QString position; QUrl qmlUrl; QObject* vm; int width = 0; };
    QVector<Item> m_items;

    QPointer<PluginFilterModel> m_leftModel;
    QPointer<PluginFilterModel> m_centerModel;
    QPointer<PluginFilterModel> m_rightModel;
};
