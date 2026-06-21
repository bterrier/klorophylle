// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalogsearchmodel.h"

#include "icatalogrepository.h"

namespace klr {

CatalogSearchModel::CatalogSearchModel(ICatalogRepository &repo, QObject *parent)
    : QAbstractListModel(parent)
    , m_repo(repo)
{
}

void CatalogSearchModel::setQuery(const QString &query)
{
    beginResetModel();
    m_rows = m_repo.search(query, kLimit);
    endResetModel();
}

int CatalogSearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CatalogSearchModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const CatalogEntry &e = m_rows.at(index.row());
    switch (role) {
    case KeyRole:        return e.key;
    case CommonNameRole: return e.commonName;
    default:             return {};
    }
}

QHash<int, QByteArray> CatalogSearchModel::roleNames() const
{
    return {
        { KeyRole, "key" },
        { CommonNameRole, "commonName" },
    };
}

} // namespace klr
