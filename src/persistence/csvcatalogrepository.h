// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalogentry.h"
#include "icatalogrepository.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QString>

// The catalog backed by the parsed CSV. Construct it from a parsed entry list (so
// tests inject a small fixture); loadBundledCatalog() builds one from the resource
// shipped in this library. Folded (lower-cased, diacritic-stripped) names are
// precomputed once so search() stays allocation-light per keystroke.
namespace klr {

class CsvCatalogRepository final : public ICatalogRepository {
public:
    explicit CsvCatalogRepository(QList<CatalogEntry> entries);

    QList<CatalogEntry> search(const QString &query, int limit) const override;
    std::optional<CatalogEntry> byKey(const QString &key) const override;
    int count() const override { return int(m_entries.size()); }

private:
    struct Folded {
        QString key;
        QString commonName;
    };

    QList<CatalogEntry> m_entries;
    QList<Folded> m_folded;          // aligned with m_entries by index
    QHash<QString, int> m_indexByKey; // botanical name -> index
};

// Reads the bundled :/klr/catalog/plantdb.csv resource and parses it.
// Returns an empty catalog if the resource is missing.
CsvCatalogRepository loadBundledCatalog();

} // namespace klr
