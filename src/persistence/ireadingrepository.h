// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "binding.h" // PlantSensorBinding (klr_core)
#include "ids.h"
#include "reading.h"

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <optional>
#include <span>

// The repository boundary: the ONLY code that touches storage lives behind this
// The domain/UI never see SQL.
//
// Readings are stored keyed on the SENSOR (never the plant). The plant a reading
// belongs to is derived through the time-bounded binding window — so a swap re-homes
// history to the plant, and a shared sensor's sample is stored ONCE yet attributes to
// every plant bound at that instant (ADR 0005). The caller supplies the plant's
// bindings (from IBindingRepository); this layer never reaches into another repo.
namespace klr {

class IReadingRepository {
public:
    virtual ~IReadingRepository() = default;

    // --- Sensor-keyed storage ---
    virtual void append(SensorId sensor, std::span<const Reading> readings) = 0;
    // All rows for (sensor, quantity) in [from, to], oldest first (absent values kept).
    virtual QList<Reading> history(SensorId sensor, Quantity quantity, const QDateTime &from,
                                   const QDateTime &to) const = 0;
    // Most recent row for (sensor, quantity) whose value is present; nullopt if none.
    virtual std::optional<Reading> latest(SensorId sensor, Quantity quantity) const = 0;

    // --- Plant-facing reads (history follows the plant) ---
    // Every reading of `quantity` from a sensor bound to the plant during [from, to],
    // clipped to each binding's [validFrom, validTo) window, merged oldest-first. A
    // role-restricted binding only contributes its role quantity.
    virtual QList<Reading> seriesForPlant(std::span<const PlantSensorBinding> bindings,
                                          Quantity quantity, const QDateTime &from,
                                          const QDateTime &to) const = 0;
    // One current value per quantity any bound sensor reports: explicit-role bindings
    // take precedence for their quantity, then the freshest sample wins (NewestWins).
    virtual QList<Reading> currentForPlant(std::span<const PlantSensorBinding> bindings) const = 0;

    // Delete every reading keyed on this sensor (data hygiene — silent cleanup that
    // accompanies a sensor delete; readings carry no change_log, so neither does this).
    // The SQLite schema would cascade these on the sensor delete anyway; clearing them
    // explicitly lets the in-memory fake reach the same observable state.
    virtual void removeForSensor(SensorId sensor) = 0;
};

} // namespace klr
