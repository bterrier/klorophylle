// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "carestatus.h" // CareRange, CareStatus, CareLevel
#include "reading.h"     // Reading

#include <QtCore/QDateTime>
#include <QtCore/QList>

namespace klr {

struct Plant;
class IBindingRepository;
class IReadingRepository;
class ICareThresholdRepository;
class ICatalogRepository;

// A plant's care snapshot at one instant: the current value per quantity (NewestWins
// across the plant's active sensors), the effective ranges judged against (the species'
// catalog ideals overlaid with per-plant overrides), the per-quantity verdicts (parallel
// to `current`), and their worst-of rollup.
//
// This is the orchestration shared by the plant-list health pill (PlantListModel) and the
// notification evaluator (AlertController) — extracted so neither re-derives the binding/
// reading/range fetch. The per-quantity dispatch itself still lives once in statusForReading
// (klr_core, ADR 0009); this only assembles its inputs from the repositories.
//
// `ranges` empty ⇒ nothing to judge: `statuses` is empty and `level` is Unknown, but
// `current` is still populated (so the home card can show a value with no bar).
struct PlantCareSnapshot {
    QList<Reading> current;     // one aggregated reading per reported quantity
    QList<CareRange> ranges;    // effective ranges (empty ⇒ nothing to judge)
    QList<CareStatus> statuses; // parallel to `current`; empty when `ranges` is empty
    CareLevel level = CareLevel::Unknown;
};

// Compute a plant's care snapshot at `now` from the repositories. `catalog` may be null
// (then only the override thresholds apply). Pure aside from the repository reads; the
// clock is injected as `now` by the caller (PlantListModel/AlertController read it once).
PlantCareSnapshot evaluatePlantCare(const Plant &plant, IBindingRepository &bindings,
                                    IReadingRepository &readings,
                                    ICareThresholdRepository &thresholds,
                                    const ICatalogRepository *catalog, const QDateTime &now);

} // namespace klr
