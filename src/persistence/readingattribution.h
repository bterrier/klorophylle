// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "binding.h" // PlantSensorBinding, isActiveAt (klr_core)
#include "ids.h"
#include "reading.h"

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <functional>
#include <optional>
#include <span>

// Plant-facing reading resolution, shared by BOTH reading-repository impls so they
// attribute identically (the parity is structural, not just tested). It is expressed
// purely over the sensor-keyed primitives — a `history` and a `latest` callback — and
// the time-bounded bindings; it never touches storage itself. See ADR 0005.
namespace klr::detail {

using HistoryFn =
    std::function<QList<Reading>(SensorId, Quantity, const QDateTime &, const QDateTime &)>;
using LatestFn = std::function<std::optional<Reading>(SensorId, Quantity)>;

// Every reading of `quantity` from a sensor bound to the plant during [from, to],
// clipped to each binding's [validFrom, validTo) window (via the same isActiveAt the
// active-binding query uses), merged oldest-first. A role-restricted binding only
// contributes its role quantity.
QList<Reading> seriesForPlant(std::span<const PlantSensorBinding> bindings, Quantity quantity,
                              const QDateTime &from, const QDateTime &to, const HistoryFn &history);

// One value per quantity any bound sensor reports. Explicit-role bindings take
// precedence for their quantity (no-role sensors then fill only the unclaimed
// quantities); the freshest sample wins (NewestWins).
QList<Reading> currentForPlant(std::span<const PlantSensorBinding> bindings,
                               const LatestFn &latest);

} // namespace klr::detail
