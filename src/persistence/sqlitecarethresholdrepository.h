// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "icarethresholdrepository.h"

// The SQLite-backed care-threshold repository — the ONLY threshold code that touches
// SQL. Each mutation and its change_log row run in one transaction; all parameters
// bound. The plant's whole threshold set is the syncable unit (change_log entity
// "careThresholds", entity_id = plantId), matching the domain model (ADR 0009).
namespace klr {

class SqliteCareThresholdRepository final : public ICareThresholdRepository {
public:
    explicit SqliteCareThresholdRepository(Database &db) : m_db(db) {}

    QList<CareRange> thresholdsFor(PlantId plant) const override;
    void setRange(PlantId plant, const CareRange &range) override;
    void replaceAll(PlantId plant, std::span<const CareRange> ranges) override;
    void clear(PlantId plant) override;

private:
    // Re-log the plant's resulting set as one change_log row (entity_id = plantId).
    void logSet(PlantId plant, const QString &op);

    Database &m_db;
};

} // namespace klr
