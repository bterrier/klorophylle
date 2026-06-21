// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bucket.h" // WriteCadenceGate (owned by value)
#include "ids.h"    // SensorId
#include "reading.h"
#include "sensor.h" // HandleKind

#include <QtCore/QString>
#include <optional>
#include <span>

namespace klr {

class ISensorRepository;
class IReadingRepository;
class Clock;

// The always-on, sensor-level ingestion seam (ADR 0011). A broadcast is persisted for ANY
// registered sensor (one the user has paired — resolvable via ISensorRepository::findByHandle),
// the entire time the app runs, regardless of which screen is open and regardless of how many
// plants the sensor feeds. Broadcasts from unpaired BLE devices in range are dropped (no Sensor
// row). The plant attribution is NOT this class's concern: `readings` is keyed by SensorId and the
// reading repository fans a sample out to every plant bound during its window (readingattribution).
//
// Plant-agnostic and non-QML, so it is unit-testable with the in-memory repository fakes. It owns
// the WriteCadenceGate (moved out of PlantCareModel): the gate is now process-global, so dedup is
// correct across plant navigation and across the N plants a sensor feeds — not reset per screen.
// Correctness of history does not depend on the gate (the repo buckets every row it is handed,
// ADR 0006); the gate remains only the write-rate optimisation the process-global dedup fix made safe.
class ReadingIngester {
public:
    ReadingIngester(ISensorRepository &sensors, IReadingRepository &readings, const Clock &clock);

    // Resolve handle -> registered Sensor; gate per (SensorId, Quantity); append the admitted
    // readings. Returns the SensorId actually written, or nullopt when the handle is unregistered
    // OR every reading was a redundant in-bucket repeat (so the caller can skip a UI refresh).
    std::optional<SensorId> ingest(HandleKind kind, const QString &handleValue,
                                   std::span<const Reading> readings);

private:
    ISensorRepository &m_sensors;
    IReadingRepository &m_readings;
    const Clock &m_clock;
    WriteCadenceGate m_gate; // process-lifetime dedup, keyed per (SensorId, Quantity)
};

} // namespace klr
