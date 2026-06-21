// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <functional>
#include <vector>

// Transactional, fail-loud schema migrations — the fix for the half-applied-migration failure mode
// where an untransacted migration could half-apply and still bump the version
// Each step runs inside transaction(); any
// failed exec() rolls back and THROWS, and schema_version only advances after a
// clean commit. No silent DROP: a destructive change is an explicit data-move step.
namespace klr {

struct Migration {
    int version;
    const char *name;
    std::function<void(QSqlQuery &)> up; // throws StorageError on failure
};

class MigrationRunner {
public:
    MigrationRunner(QSqlDatabase db, std::vector<Migration> migrations);

    int currentVersion() const; // 0 if the schema_version table does not exist yet
    void migrateTo(int target); // applies every pending step up to target, in order

private:
    void ensureVersionTable();

    QSqlDatabase m_db;
    std::vector<Migration> m_migrations;
};

} // namespace klr
