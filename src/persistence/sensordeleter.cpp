// SPDX-License-Identifier: GPL-3.0-or-later
#include "sensordeleter.h"

#include "binding.h" // PlantSensorBinding
#include "ibindingrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"

namespace klr {

SensorDeleter::SensorDeleter(ISensorRepository &sensors, IBindingRepository &bindings,
                             IReadingRepository &readings)
    : m_sensors(sensors), m_bindings(bindings), m_readings(readings)
{
}

std::expected<void, SensorDeleteError> SensorDeleter::remove(SensorId sensor)
{
    if (!m_sensors.get(sensor).has_value())
        return std::unexpected(SensorDeleteError::NotFound);

    // Refuse while ANY binding references this sensor — open OR closed. A closed binding
    // means a still-existing plant was bound to it in the past, and that plant's history
    // resolves this sensor's readings through the binding window (ADR 0005): deleting them
    // would tear a hole in that plant's history. Detaching only CLOSES a binding (sets
    // validTo) — the row, and the plant's claim on the data, remain. A binding is only ever
    // removed when its plant is deleted (FK cascade), so "no bindings" == "no plant uses it"
    // == a true orphan, which is the only safe time to delete.
    if (!m_bindings.bindingsForSensor(sensor).isEmpty())
        return std::unexpected(SensorDeleteError::StillBound);

    // Clear the dependent data first, then the sensor row (the logged root). On SQLite
    // the sensor delete's ON DELETE CASCADE would cover the first two anyway.
    m_readings.removeForSensor(sensor);
    m_bindings.removeForSensor(sensor);
    m_sensors.remove(sensor);
    return {};
}

} // namespace klr
