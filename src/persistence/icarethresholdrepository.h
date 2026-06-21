// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // klr_core: CareRange
#include "ids.h"

#include <QtCore/QList>

#include <span>

// The repository boundary for per-plant care thresholds (ADR 0009). The plant's
// thresholds are the single mutable owner of "what this plant alerts on" — one
// CareRange per (plant, quantity), seeded from the catalog species but overridable.
// The whole per-plant row-set is the syncable unit (keyed by
// plantId), matching the domain model. Only code behind this touches SQL; the
// UI/judgment never see storage.
//
// The in-memory fake and the SQLite impl pass the SAME behavioural suite, so they
// store/clear/upsert identically.
namespace klr {

class ICareThresholdRepository {
public:
    virtual ~ICareThresholdRepository() = default;

    // Every range currently set for this plant (order unspecified).
    virtual QList<CareRange> thresholdsFor(PlantId plant) const = 0;

    // Upsert one quantity's range. A range with NEITHER bound set removes the row —
    // there is nothing to judge against, and it keeps the table sparse.
    virtual void setRange(PlantId plant, const CareRange &range) = 0;

    // Replace the plant's WHOLE set atomically (the seed-from-species path): existing
    // rows are cleared, then `ranges` (those with a bound) are inserted.
    virtual void replaceAll(PlantId plant, std::span<const CareRange> ranges) = 0;

    // Drop all thresholds for the plant.
    virtual void clear(PlantId plant) = 0;
};

} // namespace klr
