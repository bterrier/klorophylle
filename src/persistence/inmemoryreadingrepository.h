// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ireadingrepository.h"

#include <QtCore/QHash>
#include <QtCore/QUuid>

namespace klr {

// In-memory fake: lets the whole domain be unit-tested with no SQLite / BLE / QML
// (coverage was narrow precisely because
// nothing could be tested without a real DB). Stores readings keyed on the sensor;
// the plant-facing reads delegate to the shared klr::detail attribution helpers, so
// the fake and SqliteReadingRepository resolve identically.
class InMemoryReadingRepository final : public IReadingRepository {
public:
    void append(SensorId sensor, std::span<const Reading> readings) override;
    QList<Reading> history(SensorId sensor, Quantity quantity, const QDateTime &from,
                           const QDateTime &to) const override;
    std::optional<Reading> latest(SensorId sensor, Quantity quantity) const override;
    QList<Reading> seriesForPlant(std::span<const PlantSensorBinding> bindings, Quantity quantity,
                                  const QDateTime &from, const QDateTime &to) const override;
    QList<Reading> currentForPlant(std::span<const PlantSensorBinding> bindings) const override;
    void removeForSensor(SensorId sensor) override;

private:
    QHash<QUuid, QList<Reading>> m_bySensor; // keyed by SensorId::value
};

} // namespace klr
