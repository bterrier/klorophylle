// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

#include <optional>
#include <span>

// Pure multi-sensor aggregation — collapse readings of ONE quantity, gathered from one
// or more sensors bound to a plant, into a single current value. No DB/BLE/QML; literal
// inputs, unit-tested. See ../../docs/adr/0005-plant-sensor-binding.md.
namespace klr {

enum class AggregationPolicy {
    NewestWins, // the freshest sample wins (implemented)
    Average,    // mean of freshest-per-sensor within a window — deferred (behind a setting)
};

// Collapse `readings` (expected to share one Quantity; absent values are ignored) to a
// single Reading per the policy. Returns nullopt when there is no present value.
// NewestWins picks the latest timestamp. Average is not yet implemented and falls back
// to NewestWins (ship newest-wins; averaging is planned behind a setting).
std::optional<Reading> aggregate(std::span<const Reading> readings, AggregationPolicy policy);

} // namespace klr
