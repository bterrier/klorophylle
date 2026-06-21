// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ids.h"
#include "sensor.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <optional>

// The repository boundary for physical sensors (the ONLY code that touches storage
// lives behind this). A sensor is plant-agnostic
// — the plant<->sensor relation is the binding repository, not a field here.
//
// `ensure` is the dedup seam between the live device layer (which knows a raw BLE
// handle) and the app's stable SensorId: same handle in -> same SensorId out.
namespace klr {

class ISensorRepository {
public:
    virtual ~ISensorRepository() = default;

    // Return the SensorId for this handle, minting + storing a new sensor on first
    // sight (dedup on (handle_kind, handle_value), never assuming a MAC).
    virtual SensorId ensure(HandleKind kind, const QString &handleValue, const QString &model) = 0;

    // Insert a sensor PRESERVING its app-minted id (upsert by id: update if the id
    // already exists, else insert). The backup-restore seam (ADR 0010): `ensure` would
    // mint a NEW id and orphan the backup's bindings/readings keyed on the old SensorId,
    // so restore must keep the original id. The live BLE path keeps using `ensure`.
    virtual void add(const Sensor &sensor) = 0;

    virtual std::optional<Sensor> get(SensorId id) const = 0;
    virtual std::optional<Sensor> findByHandle(HandleKind kind, const QString &handleValue) const = 0;
    virtual QList<Sensor> all() const = 0;

    // Delete the sensor row (the logged root of a sensor's removal — data hygiene).
    // The SQLite schema cascades the sensor's readings + bindings (ON DELETE CASCADE);
    // the SensorDeleter use-case clears those explicitly first so the in-memory fakes
    // reach the same observable state. A no-op for an unknown id.
    virtual void remove(SensorId id) = 0;
};

} // namespace klr
