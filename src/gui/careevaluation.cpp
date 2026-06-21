// SPDX-License-Identifier: GPL-3.0-or-later
#include "careevaluation.h"

#include "binding.h" // PlantSensorBinding
#include "catalogthresholds.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "icatalogrepository.h"
#include "ireadingrepository.h"
#include "plant.h"

#include <span>

namespace klr {

PlantCareSnapshot evaluatePlantCare(const Plant &plant, IBindingRepository &bindings,
                                    IReadingRepository &readings,
                                    ICareThresholdRepository &thresholds,
                                    const ICatalogRepository *catalog, const QDateTime &now)
{
    PlantCareSnapshot snap;
    // Effective ranges = the species' catalog ideals overlaid with any per-plant override.
    // Data-driven: a speciesed plant is judged immediately, with no seeding step (ADR 0003).
    snap.ranges = effectiveRanges(catalog, plant.species, thresholds.thresholdsFor(plant.id));

    const QList<PlantSensorBinding> active = bindings.activeFor(plant.id, now);
    snap.current = readings.currentForPlant(
        std::span<const PlantSensorBinding>(active.constData(), active.size()));

    if (snap.ranges.isEmpty())
        return snap; // no species ideals and no overrides — nothing to judge (level Unknown)

    // The peak/extremes/current dispatch lives once in klr_core (statusForReading); we only
    // supply the recent-window fetch over the plant's bindings (open & closed, so a windowed
    // quantity — light, temperature — follows the plant across sensor swaps). A reading with
    // no range judges Unknown, which the rollup ignores — same as skipping it.
    const QList<PlantSensorBinding> all = bindings.bindings(plant.id);
    const std::span<const CareRange> rangeSpan(snap.ranges.constData(), snap.ranges.size());
    const ReadingWindowFn window = [&](Quantity q, const QDateTime &from, const QDateTime &to) {
        return readings.seriesForPlant(
            std::span<const PlantSensorBinding>(all.constData(), all.size()), q, from, to);
    };
    snap.statuses.reserve(snap.current.size());
    for (const Reading &r : snap.current)
        snap.statuses.append(statusForReading(r, rangeSpan, now, window));
    snap.level =
        rollup(std::span<const CareStatus>(snap.statuses.constData(), snap.statuses.size()));
    return snap;
}

} // namespace klr
