// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "catalogentry.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QList>

// A PURE parser for the bundled plant-catalog CSV (no I/O, no Qt SQL) — unit-tested
// against inline fixtures, like the device AdvertisementParser. The repository reads
// the bundled resource and hands the bytes here.
//
// Format: ';'-delimited, UTF-8, one header row then one plant per line. The decimal
// separator is '.', and ',' only appears inside list columns we don't parse, so a
// plain split on ';' is safe. Blank numeric cells become nullopt; rows with an empty
// botanical name are skipped.
namespace klr {

struct CatalogCsv {
    static QList<CatalogEntry> parse(QByteArrayView csv);
};

} // namespace klr
