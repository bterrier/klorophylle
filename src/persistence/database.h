// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "clock.h"

#include <QtCore/QString>
#include <QtSql/QSqlDatabase>

// Owns a single SQLite connection for the process (one named QSqlDatabase). The
// per-thread ConnectionPool is deferred until the headless probe actually shares
// the file. On open we apply, as PRAGMAs:
//   foreign_keys=ON, journal_mode=WAL, busy_timeout, synchronous=NORMAL.
//
// Database holds only the connection NAME (never a long-lived QSqlDatabase copy),
// so removeDatabase() in the destructor never warns about a connection still in
// use. Callers fetch a transient handle() per operation.
namespace klr {

class Database {
public:
    // Opens (creating if needed) a SQLite file. Throws StorageError on failure.
    static Database openFile(const QString &path, const Clock &clock);
    // In-memory database, for tests. Throws StorageError on failure.
    static Database openInMemory(const Clock &clock);

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&other) noexcept;
    Database &operator=(Database &&other) noexcept;
    ~Database();

    QSqlDatabase handle() const;            // transient connection copy
    const Clock &clock() const { return *m_clock; }
    qint64 nowMs() const { return m_clock->nowMs(); }

    // Per-install replica id (UUIDv7), minted + persisted in app_meta on first use.
    // Requires the v1 schema (app_meta) to exist.
    QString replicaId();

private:
    Database(QString connectionName, const Clock &clock);
    void applyPragmas();
    void close();

    QString m_connName;
    const Clock *m_clock;
    QString m_replicaId; // cached after first replicaId()
};

} // namespace klr
