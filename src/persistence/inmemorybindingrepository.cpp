// SPDX-License-Identifier: GPL-3.0-or-later
#include "inmemorybindingrepository.h"
#include "storageerror.h"

#include <span>

namespace klr {

void InMemoryBindingRepository::bind(PlantId plant, SensorId sensor, const QDateTime &validFrom,
                                     std::optional<Quantity> role)
{
    const QList<PlantSensorBinding> existing = bindings(plant);
    const PlantSensorBinding candidate{ plant, sensor, validFrom, std::nullopt, role };

    const auto ok = validateBinding(
        std::span<const PlantSensorBinding>(existing.constData(), existing.size()), candidate);
    if (!ok)
        throw StorageError(QStringLiteral("binding rejected: role conflict for this plant"));

    m_bindings.append(candidate);
}

void InMemoryBindingRepository::unbind(PlantId plant, SensorId sensor, const QDateTime &validTo)
{
    for (PlantSensorBinding &b : m_bindings) {
        if (b.plant == plant && b.sensor == sensor && !b.validTo.has_value())
            b.validTo = validTo; // close the open edge for this pair
    }
}

QList<PlantSensorBinding> InMemoryBindingRepository::activeFor(PlantId plant,
                                                              const QDateTime &at) const
{
    const QList<PlantSensorBinding> forPlant = bindings(plant);
    return activeBindings(std::span<const PlantSensorBinding>(forPlant.constData(), forPlant.size()),
                          at);
}

QList<PlantSensorBinding> InMemoryBindingRepository::bindings(PlantId plant) const
{
    QList<PlantSensorBinding> out;
    for (const PlantSensorBinding &b : m_bindings) {
        if (b.plant == plant)
            out.append(b);
    }
    return out;
}

QList<PlantSensorBinding> InMemoryBindingRepository::bindingsForSensor(SensorId sensor) const
{
    QList<PlantSensorBinding> out;
    for (const PlantSensorBinding &b : m_bindings) {
        if (b.sensor == sensor)
            out.append(b);
    }
    return out;
}

void InMemoryBindingRepository::removeForSensor(SensorId sensor)
{
    m_bindings.removeIf([&](const PlantSensorBinding &b) { return b.sensor == sensor; });
}

} // namespace klr
