// LauncherViewModel.cpp
#include "LauncherViewModel.h"
#include <QProcess>
#include <QDebug>

LauncherViewModel::LauncherViewModel(QObject* parent)
    : QAbstractListModel(parent) {}

int LauncherViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant LauncherViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const auto& it = m_items.at(index.row());
    switch (role) {
        case NameRole:    return it.name;
        case ProgramRole: return it.program;
        case IconRole:    return it.icon;
    }
    return {};
}

QHash<int, QByteArray> LauncherViewModel::roleNames() const {
    return { { NameRole, "name" }, { ProgramRole, "program" }, { IconRole, "icon" } };
}

void LauncherViewModel::launch(int index) {
    if (index < 0 || index >= m_items.size()) return;
    const auto& it = m_items.at(index);
    qDebug() << "[Launcher] launching:" << it.program;
    if (!QProcess::startDetached(it.program, {})) {
        qWarning() << "[Launcher] failed to launch:" << it.program;
    }
}

void LauncherViewModel::setEntries(const QList<Entry>& items) {
    beginResetModel();
    m_items = items;
    endResetModel();
}
