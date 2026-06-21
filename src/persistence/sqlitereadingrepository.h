// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "ireadingrepository.h"

// The SQLite-backed reading repository. The ONLY reading code that touches SQL. Rows
// key on sensor_id; the plant-facing reads delegate to the shared klr::detail
// attribution helpers (same logic as the in-memory fake), so the two never diverge.
//
// Readings are bulk telemetry: unlike the user-authored entities (plant/journal/
// sensor/binding) they do NOT write change_log rows here — reading sync is a later
// phase. append() floors each reading's timestamp to its ts_bucket (one row per
// bucket) and upserts: the latest present sample wins, an absent value never erases a
// stored one, and source/observed_by carry provenance (ADR 0006).
namespace klr {

class SqliteReadingRepository final : public IReadingRepository {
public:
    explicit SqliteReadingRepository(Database &db) : m_db(db) {}

    void append(SensorId sensor, std::span<const Reading> readings) override;
    QList<Reading> history(SensorId sensor, Quantity quantity, const QDateTime &from,
                           const QDateTime &to) const override;
    std::optional<Reading> latest(SensorId sensor, Quantity quantity) const override;
    QList<Reading> seriesForPlant(std::span<const PlantSensorBinding> bindings, Quantity quantity,
                                  const QDateTime &from, const QDateTime &to) const override;
    QList<Reading> currentForPlant(std::span<const PlantSensorBinding> bindings) const override;
    void removeForSensor(SensorId sensor) override;

private:
    Database &m_db;
};

} // namespace klr
