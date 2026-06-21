// SPDX-License-Identifier: GPL-3.0-or-later
#include "migrationrunner.h"
#include "sqlsupport.h"
#include "storageerror.h"

#include <QtSql/QSqlError>
#include <algorithm>

namespace klr {

MigrationRunner::MigrationRunner(QSqlDatabase db, std::vector<Migration> migrations)
    : m_db(std::move(db)), m_migrations(std::move(migrations))
{
    std::sort(m_migrations.begin(), m_migrations.end(),
              [](const Migration &a, const Migration &b) { return a.version < b.version; });
}

void MigrationRunner::ensureVersionTable()
{
    QSqlQuery q(m_db);
    detail::execOrThrow(q, QStringLiteral(
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"));

    QSqlQuery count(m_db);
    detail::execOrThrow(count, QStringLiteral("SELECT COUNT(*) FROM schema_version"));
    if (!count.next())
        throw StorageError(QStringLiteral("schema_version count returned no row"));
    if (count.value(0).toInt() == 0) {
        QSqlQuery seed(m_db);
        detail::execOrThrow(seed, QStringLiteral("INSERT INTO schema_version(version) VALUES(0)"));
    }
}

int MigrationRunner::currentVersion() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && q.next())
        return q.value(0).toInt();
    return 0;
}

void MigrationRunner::migrateTo(int target)
{
    ensureVersionTable();
    const int current = currentVersion();

    for (const Migration &m : m_migrations) {
        if (m.version <= current || m.version > target)
            continue;

        if (!m_db.transaction())
            throw StorageError(QStringLiteral("could not begin transaction for migration %1 (%2)")
                                   .arg(m.version)
                                   .arg(QString::fromUtf8(m.name)));
        try {
            QSqlQuery q(m_db);
            m.up(q); // throws StorageError on any failed step

            QSqlQuery bump(m_db);
            detail::prepareOrThrow(bump, QStringLiteral("UPDATE schema_version SET version = :v"));
            bump.bindValue(QStringLiteral(":v"), m.version);
            detail::execPreparedOrThrow(bump);

            if (!m_db.commit()) {
                const QString err = m_db.lastError().text();
                m_db.rollback();
                throw StorageError(QStringLiteral("commit failed for migration %1: %2")
                                       .arg(m.version)
                                       .arg(err));
            }
        } catch (...) {
            m_db.rollback(); // never leave schema_version advanced past a failed step
            throw;
        }
    }
}

} // namespace klr
