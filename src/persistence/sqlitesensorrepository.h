// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"
#include "isensorrepository.h"

// The SQLite-backed sensor repository. The ONLY sensor code that touches SQL. Each
// mutation and its change_log row run in one transaction; all parameters bound.
namespace klr {

class SqliteSensorRepository final : public ISensorRepository {
public:
    explicit SqliteSensorRepository(Database &db) : m_db(db) {}

    SensorId ensure(HandleKind kind, const QString &handleValue, const QString &model) override;
    void add(const Sensor &sensor) override;
    std::optional<Sensor> get(SensorId id) const override;
    std::optional<Sensor> findByHandle(HandleKind kind, const QString &handleValue) const override;
    QList<Sensor> all() const override;
    void remove(SensorId id) override;

private:
    Database &m_db;
};

} // namespace klr
