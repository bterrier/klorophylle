// SPDX-License-Identifier: GPL-3.0-or-later
#include "readingingester.h"

#include "clock.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "log.h"
#include "sensor.h"
#include "storageerror.h"

#include <QtCore/QList>

namespace klr {

ReadingIngester::ReadingIngester(ISensorRepository &sensors, IReadingRepository &readings,
                                 const Clock &clock)
    : m_sensors(sensors)
    , m_readings(readings)
    , m_clock(clock)
{
}

std::optional<SensorId> ReadingIngester::ingest(HandleKind kind, const QString &handleValue,
                                                std::span<const Reading> readings)
{
    if (readings.empty())
        return std::nullopt;

    // Only registered sensors are persisted: a broadcast from an unpaired device in range has no
    // Sensor row, so it is dropped (it never reaches a plant). The registered-sensors set manages the paired-but-unbound case.
    const std::optional<Sensor> sensor = m_sensors.findByHandle(kind, handleValue);
    if (!sensor)
        return std::nullopt;

    // Drop identical re-broadcasts before they reach the DB: keep only the readings the per-series
    // gate admits (a new bucket, or a changed value). The gate is keyed on the stable SensorId, so
    // dedup survives plant navigation. The repo still buckets/upserts whatever survives (ADR 0006).
    const QString key = sensor->id.toString();
    QList<Reading> admitted;
    for (const Reading &r : readings) {
        const qint64 nowMs =
            r.timestamp.isValid() ? r.timestamp.toMSecsSinceEpoch() : m_clock.nowMs();
        if (m_gate.admit(key, r.quantity, nowMs, r.value))
            admitted.append(r);
    }
    if (admitted.isEmpty())
        return std::nullopt; // every reading was a redundant repeat — nothing to store

    try {
        m_readings.append(sensor->id, admitted);
    } catch (const StorageError &e) {
        qCWarning(lcApp) << "ingest reading failed:" << e.what();
        return std::nullopt;
    }
    return sensor->id;
}

} // namespace klr
