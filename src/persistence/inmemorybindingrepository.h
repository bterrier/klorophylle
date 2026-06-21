// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ibindingrepository.h"

namespace klr {

// The test/fake binding repository. Reuses the pure klr::activeBindings /
// validateBinding logic, so the fake and the SQLite impl resolve identically.
class InMemoryBindingRepository final : public IBindingRepository {
public:
    void bind(PlantId plant, SensorId sensor, const QDateTime &validFrom,
              std::optional<Quantity> role) override;
    void unbind(PlantId plant, SensorId sensor, const QDateTime &validTo) override;
    QList<PlantSensorBinding> activeFor(PlantId plant, const QDateTime &at) const override;
    QList<PlantSensorBinding> bindings(PlantId plant) const override;
    QList<PlantSensorBinding> bindingsForSensor(SensorId sensor) const override;
    void removeForSensor(SensorId sensor) override;

private:
    QList<PlantSensorBinding> m_bindings;
};

} // namespace klr
