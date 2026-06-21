// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalogentry.h"

#include <QtCore/QList>
#include <QtCore/QString>

#include <optional>

// Read-only access to the bundled plant catalog, behind the repository boundary so
// the UI never reads the CSV (or any storage) directly. Unlike the mutable app data,
// the catalog is shipped reference data with no SQL — CsvCatalogRepository is the one
// implementation; this interface exists as the injection/test seam.
namespace klr {

class ICatalogRepository {
public:
    virtual ~ICatalogRepository() = default;

    // Ranked species matches (botanical + common name), case- and diacritic-
    // insensitive, prefix before substring. Empty query -> no results.
    virtual QList<CatalogEntry> search(const QString &query, int limit) const = 0;

    // Exact lookup by the catalog key (botanical name) a Plant stores.
    virtual std::optional<CatalogEntry> byKey(const QString &key) const = 0;

    virtual int count() const = 0;
};

} // namespace klr
