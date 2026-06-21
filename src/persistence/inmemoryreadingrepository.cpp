// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemoryreadingrepository.h"
#include "bucket.h"
#include "readingattribution.h"

#include <algorithm>

namespace klr {

void InMemoryReadingRepository::append(SensorId sensor, std::span<const Reading> readings)
{
    QList<Reading> &list = m_bySensor[sensor.value];
    for (const Reading &r : readings) {
        // Mirror the SQLite ON CONFLICT policy exactly so the parity suite stays honest:
        // one row per (quantity, bucket); the latest present sample wins; an absent
        // (NULL) value never erases a stored present one (ADR 0006).
        const qint64 bucket = bucketStartMs(r.timestamp.toMSecsSinceEpoch());
        const auto pos = std::find_if(list.begin(), list.end(), [&](const Reading &e) {
            return e.quantity == r.quantity
                   && bucketStartMs(e.timestamp.toMSecsSinceEpoch()) == bucket;
        });
        if (pos == list.end())
            list.append(r);              // first sample in this bucket (NULL kept)
        else if (r.value.has_value())
            *pos = r;                    // latest present value wins
    }
}

QList<Reading> InMemoryReadingRepository::history(SensorId sensor, Quantity quantity,
                                                  const QDateTime &from, const QDateTime &to) const
{
    QList<Reading> out;
    const auto it = m_bySensor.constFind(sensor.value);
    if (it == m_bySensor.cend())
        return out;
    for (const Reading &r : *it) {
        if (r.quantity == quantity && r.timestamp >= from && r.timestamp <= to)
            out.append(r);
    }
    std::sort(out.begin(), out.end(),
              [](const Reading &a, const Reading &b) { return a.timestamp < b.timestamp; });
    return out;
}

std::optional<Reading> InMemoryReadingRepository::latest(SensorId sensor, Quantity quantity) const
{
    const auto it = m_bySensor.constFind(sensor.value);
    if (it == m_bySensor.cend())
        return std::nullopt;

    const Reading *best = nullptr;
    for (const Reading &r : *it) {
        if (r.quantity != quantity || !r.value.has_value())
            continue; // most recent PRESENT value
        if (!best || r.timestamp > best->timestamp)
            best = &r;
    }
    if (!best)
        return std::nullopt;
    return *best;
}

QList<Reading> InMemoryReadingRepository::seriesForPlant(
    std::span<const PlantSensorBinding> bindings, Quantity quantity, const QDateTime &from,
    const QDateTime &to) const
{
    return detail::seriesForPlant(
        bindings, quantity, from, to,
        [this](SensorId s, Quantity q, const QDateTime &f, const QDateTime &t) {
            return history(s, q, f, t);
        });
}

QList<Reading> InMemoryReadingRepository::currentForPlant(
    std::span<const PlantSensorBinding> bindings) const
{
    return detail::currentForPlant(
        bindings, [this](SensorId s, Quantity q) { return latest(s, q); });
}

void InMemoryReadingRepository::removeForSensor(SensorId sensor)
{
    m_bySensor.remove(sensor.value);
}

} // namespace klr
