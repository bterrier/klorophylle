// SPDX-License-Identifier: GPL-3.0-or-later
#include "readingattribution.h"

#include "aggregate.h" // klr::aggregate (klr_core)

#include <algorithm>

namespace klr::detail {

QList<Reading> seriesForPlant(std::span<const PlantSensorBinding> bindings, Quantity quantity,
                              const QDateTime &from, const QDateTime &to, const HistoryFn &history)
{
    QList<Reading> out;
    for (const PlantSensorBinding &b : bindings) {
        if (b.role.has_value() && *b.role != quantity)
            continue; // a role-restricted sensor doesn't supply this quantity
        const QList<Reading> rows = history(b.sensor, quantity, from, to);
        for (const Reading &r : rows) {
            // Clip to the binding window with the SAME rule as the active-binding query,
            // so a swap re-homes history to exactly the right plant.
            if (isActiveAt(b, r.timestamp))
                out.append(r);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Reading &a, const Reading &b) { return a.timestamp < b.timestamp; });
    return out;
}

QList<Reading> currentForPlant(std::span<const PlantSensorBinding> bindings, const LatestFn &latest)
{
    QList<Reading> out;
    for (int i = 0; i < kQuantityCount; ++i) {
        const auto quantity = static_cast<Quantity>(i);

        // Explicit-role bindings claim their quantity; if any does, no-role sensors do
        // not contribute to that quantity.
        bool hasExplicitForQuantity = false;
        for (const PlantSensorBinding &b : bindings) {
            if (b.role == quantity)
                hasExplicitForQuantity = true;
        }

        QList<Reading> gathered;
        for (const PlantSensorBinding &b : bindings) {
            const bool supplies =
                b.role.has_value() ? (*b.role == quantity) : !hasExplicitForQuantity;
            if (!supplies)
                continue;
            if (const std::optional<Reading> r = latest(b.sensor, quantity))
                gathered.append(*r);
        }

        if (const std::optional<Reading> agg = aggregate(
                std::span<const Reading>(gathered.constData(), gathered.size()),
                AggregationPolicy::NewestWins))
            out.append(*agg);
    }
    return out;
}

} // namespace klr::detail
