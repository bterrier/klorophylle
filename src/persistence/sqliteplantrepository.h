// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "iplantrepository.h"

// The SQLite-backed plant repository. The ONLY plant code that touches SQL. Each
// mutation and its change_log row run in one transaction; all parameters bound.
namespace klr {

class SqlitePlantRepository final : public IPlantRepository {
public:
    explicit SqlitePlantRepository(Database &db) : m_db(db) {}

    void add(const Plant &plant) override;
    void update(const Plant &plant) override;
    void remove(PlantId id) override;
    std::optional<Plant> get(PlantId id) const override;
    QList<Plant> all() const override;

private:
    Database &m_db;
};

} // namespace klr
