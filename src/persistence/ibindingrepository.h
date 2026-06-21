// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "binding.h" // PlantSensorBinding (klr_core)
#include "ids.h"
#include "reading.h" // Quantity

#include <QtCore/QDateTime>
#include <QtCore/QList>
#include <optional>

// The repository boundary for plant<->sensor bindings (the ONLY code that touches
// storage lives behind this). The relation is many-to-many both ways. `bind` enforces
// the per-plant overlap rule (klr::validateBinding); a swap is `unbind` then `bind`.
//
// Mutations that violate the overlap rule throw StorageError, so the in-memory fake
// and the SQLite impl reject identically (the shared behavioural suite checks this).
namespace klr {

class IBindingRepository {
public:
    virtual ~IBindingRepository() = default;

    virtual void bind(PlantId plant, SensorId sensor, const QDateTime &validFrom,
                      std::optional<Quantity> role) = 0;
    virtual void unbind(PlantId plant, SensorId sensor, const QDateTime &validTo) = 0;
    // Bindings active at `at` for this plant (validFrom <= at < validTo|inf).
    virtual QList<PlantSensorBinding> activeFor(PlantId plant, const QDateTime &at) const = 0;
    // Every binding for this plant, open or closed (for swap history / auditing).
    virtual QList<PlantSensorBinding> bindings(PlantId plant) const = 0;

    // Every binding (open or closed) referencing this sensor, across all plants — the
    // sensor-keyed view the registered-sensors model needs to judge bound/unbound,
    // without iterating every plant. Ordered by validFrom then id, like bindings().
    virtual QList<PlantSensorBinding> bindingsForSensor(SensorId sensor) const = 0;
    // Delete every binding referencing this sensor (silent cleanup accompanying a sensor
    // delete — the cascade equivalent of the SQLite ON DELETE CASCADE, so the in-memory
    // fake converges). Not logged on its own; the sensor delete is the logged root.
    virtual void removeForSensor(SensorId sensor) = 0;
};

} // namespace klr
