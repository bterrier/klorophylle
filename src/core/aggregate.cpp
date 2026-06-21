// SPDX-License-Identifier: GPL-3.0-or-later
#include "aggregate.h"

namespace klr {

std::optional<Reading> aggregate(std::span<const Reading> readings, AggregationPolicy policy)
{
    // TODO: AggregationPolicy::Average — mean of the freshest sample per sensor
    // within a freshness window, exposed behind a setting. Until then every policy
    // resolves to newest-wins.
    Q_UNUSED(policy)

    const Reading *newest = nullptr;
    for (const Reading &r : readings) {
        if (!r.value.has_value())
            continue; // absent is std::nullopt, never a sentinel — skip it
        if (!newest || r.timestamp > newest->timestamp)
            newest = &r;
    }
    if (!newest)
        return std::nullopt;
    return *newest;
}

} // namespace klr
