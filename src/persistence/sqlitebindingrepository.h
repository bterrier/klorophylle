// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "ibindingrepository.h"

// The SQLite-backed binding repository. The ONLY binding code that touches SQL. Each
// mutation and its change_log row run in one transaction; all parameters bound. The
// per-plant overlap rule is enforced through the pure klr::validateBinding.
namespace klr {

class SqliteBindingRepository final : public IBindingRepository {
public:
    explicit SqliteBindingRepository(Database &db) : m_db(db) {}

    void bind(PlantId plant, SensorId sensor, const QDateTime &validFrom,
              std::optional<Quantity> role) override;
    void unbind(PlantId plant, SensorId sensor, const QDateTime &validTo) override;
    QList<PlantSensorBinding> activeFor(PlantId plant, const QDateTime &at) const override;
    QList<PlantSensorBinding> bindings(PlantId plant) const override;
    QList<PlantSensorBinding> bindingsForSensor(SensorId sensor) const override;
    void removeForSensor(SensorId sensor) override;

private:
    Database &m_db;
};

} // namespace klr
