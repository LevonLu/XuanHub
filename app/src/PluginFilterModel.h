// PluginFilterModel.h — 按 position 过滤的 QSortFilterProxyModel
#pragma once
#include <QSortFilterProxyModel>

class PluginFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PluginFilterModel(const QString& position, QObject* parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_position;
};
