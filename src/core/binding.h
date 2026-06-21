// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"
#include "reading.h" // Quantity

#include <QtCore/QDateTime>
#include <QtCore/QList>

#include <expected>
#include <optional>
#include <span>

// Pure plant<->sensor binding logic — no DB, no BLE, no QML, clock-injected by the
// caller (the QDateTime is passed in). The defining mechanism of goal #1: history
// follows the PLANT, because a reading is keyed on its sensor and the plant(s) it
// belongs to are derived through these time-bounded edges. See
// ../../docs/adr/0005-plant-sensor-binding.md.
namespace klr {

// Time-bounded plant<->sensor edge. The relation is MANY-TO-MANY in both directions:
// a plant holds N concurrent sensors, AND a sensor serves N plants (two plants in one
// pot read by one shared probe). A swap = close the old edge (set validTo) + open a
// new one.
struct PlantSensorBinding {
    PlantId plant {};
    SensorId sensor {};
    QDateTime validFrom {};
    std::optional<QDateTime> validTo {}; // nullopt == currently bound
    std::optional<Quantity> role {};     // restrict which quantity this sensor supplies for this plant

    bool operator==(const PlantSensorBinding &) const = default;
};

// Active iff validFrom <= at && (no validTo || at < validTo). Half-open at the top so
// an instantaneous swap (close at T, open at T) attributes a sample at T to exactly
// one binding — the freshly-opened one.
bool isActiveAt(const PlantSensorBinding &b, const QDateTime &at);

// The subset of `all` active at `at`, order preserved.
QList<PlantSensorBinding> activeBindings(std::span<const PlantSensorBinding> all,
                                         const QDateTime &at);

enum class BindingError {
    RoleConflict, // two overlapping explicit-role bindings for one quantity (same plant)
};

// Validate a candidate against THIS PLANT's existing bindings — never the sensor's
// other plants (a probe shared by plants A and B, each pinning it to AirTemperature,
// is not a conflict). Within one plant: redundant no-role bindings for a quantity are
// allowed (two soil probes in a big pot); two overlapping *explicit-role* bindings for
// the same quantity are rejected; a no-role + explicit-role overlap is allowed (the
// role one wins in aggregation).
std::expected<void, BindingError> validateBinding(
    std::span<const PlantSensorBinding> existingForPlant,
    const PlantSensorBinding &candidate);

} // namespace klr
