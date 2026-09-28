#include "ui/RoleFilter.h"

RoleFilter::RoleFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &RoleFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &RoleFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &RoleFilter::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &RoleFilter::countChanged);
}

void RoleFilter::setRole(const QString &role)
{
    if (m_role == role)
        return;
    m_role = role;
    invalidateFilter();
    emit changed();
    emit countChanged();
}

void RoleFilter::setValue(const QString &value)
{
    if (m_value == value)
        return;
    m_value = value;
    invalidateFilter();
    emit changed();
    emit countChanged();
}

int RoleFilter::sourceRow(int row) const
{
    return mapToSource(index(row, 0)).row();
}

int RoleFilter::roleId() const
{
    if (!sourceModel())
        return -1;
    const QHash<int, QByteArray> roles = sourceModel()->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (QString::fromLatin1(it.value()) == m_role)
            return it.key();
    }
    return -1;
}

bool RoleFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_value.isEmpty() || m_role.isEmpty())
        return true;
    const int id = roleId();
    if (id < 0)
        return true;
    return sourceModel()->index(sourceRow, 0, sourceParent).data(id).toString() == m_value;
}
