// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalogentry.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>

namespace klr {

class ICatalogRepository;

// Adapts catalog search to QML: setQuery() runs the repository search and resets the
// rows the species picker binds to. Thin — the ranking lives in the repository, the
// model just exposes the hits as roles. Results are capped (a typeahead never needs
// the whole catalog on screen).
class CatalogSearchModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        KeyRole = Qt::UserRole + 1, // botanical name (the catalog key a Plant stores)
        CommonNameRole,
    };

    explicit CatalogSearchModel(ICatalogRepository &repo, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-run the search; an empty/blank query clears the results.
    void setQuery(const QString &query);

private:
    static constexpr int kLimit = 50;

    ICatalogRepository &m_repo;
    QList<CatalogEntry> m_rows;
};

} // namespace klr
