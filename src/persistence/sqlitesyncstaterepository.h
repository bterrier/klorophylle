// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "isyncstaterepository.h"

// The SQLite-backed history-sync-state repository — the ONLY sync-state code that touches SQL. A
// single `sensor_sync_state` row per sensor, upserted; device-local, so (unlike the other repos) it
// writes NO change_log row (ADR 0014). Parameters bound; reads/writes are single statements.
namespace klr {

class SqliteSyncStateRepository final : public ISyncStateRepository {
public:
    explicit SqliteSyncStateRepository(Database &db) : m_db(db) {}

    std::optional<QDateTime> lastHistorySync(SensorId sensor) const override;
    void setLastHistorySync(SensorId sensor, const QDateTime &when) override;

private:
    Database &m_db;
};

} // namespace klr
