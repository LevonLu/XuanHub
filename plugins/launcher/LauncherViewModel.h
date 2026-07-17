// LauncherViewModel.h — 启动项列表模型（QAbstractListModel + Q_INVOKABLE launch）
#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>

class LauncherViewModel : public QAbstractListModel {
    Q_OBJECT
public:
    struct Entry { QString name; QString program; QString icon; };

    enum Roles { NameRole = Qt::UserRole + 1, ProgramRole, IconRole };

    explicit LauncherViewModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void launch(int index);

    void setEntries(const QList<Entry>& items);

private:
    QList<Entry> m_items;
};
