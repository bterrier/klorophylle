// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"
#include "storageerror.h"

#include <QtCore/QUuid>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

namespace klr {

Database::Database(QString connectionName, const Clock &clock)
    : m_connName(std::move(connectionName)), m_clock(&clock)
{
}

Database::Database(Database &&other) noexcept
    : m_connName(std::move(other.m_connName)), m_clock(other.m_clock),
      m_replicaId(std::move(other.m_replicaId))
{
    other.m_connName.clear();
}

Database &Database::operator=(Database &&other) noexcept
{
    if (this != &other) {
        close();
        m_connName = std::move(other.m_connName);
        m_clock = other.m_clock;
        m_replicaId = std::move(other.m_replicaId);
        other.m_connName.clear();
    }
    return *this;
}

Database::~Database()
{
    close();
}

Database Database::openFile(const QString &path, const Clock &clock)
{
    const QString name = QStringLiteral("klr_%1")
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        const QString err = db.lastError().text();
        db = QSqlDatabase(); // drop the local handle before removing the connection
        QSqlDatabase::removeDatabase(name);
        throw StorageError(QStringLiteral("Database: open '%1' failed: %2").arg(path, err));
    }
    Database self(name, clock);
    self.applyPragmas();
    return self;
}

Database Database::openInMemory(const Clock &clock)
{
    return openFile(QStringLiteral(":memory:"), clock);
}

void Database::applyPragmas()
{
    QSqlDatabase db = handle();
    QSqlQuery q(db);
    // foreign_keys must be set per-connection; WAL + NORMAL are the GUI+probe model.
    for (const char *pragma : { "PRAGMA foreign_keys = ON",
                                "PRAGMA journal_mode = WAL",
                                "PRAGMA synchronous = NORMAL" }) {
        if (!q.exec(QString::fromLatin1(pragma))) {
            throw StorageError(QStringLiteral("Database: '%1' failed: %2")
                                   .arg(QString::fromLatin1(pragma), q.lastError().text()));
        }
    }
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connName);
}

QString Database::replicaId()
{
    if (!m_replicaId.isEmpty())
        return m_replicaId;

    QSqlDatabase db = handle();
    QSqlQuery sel(db);
    sel.prepare(QStringLiteral("SELECT value FROM app_meta WHERE key = 'replica_id'"));
    if (sel.exec() && sel.next()) {
        m_replicaId = sel.value(0).toString();
        return m_replicaId;
    }

    m_replicaId = QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO app_meta(key, value) VALUES('replica_id', :v)"));
    ins.bindValue(QStringLiteral(":v"), m_replicaId);
    if (!ins.exec()) {
        throw StorageError(QStringLiteral("Database: persisting replica_id failed: %1")
                               .arg(ins.lastError().text()));
    }
    return m_replicaId;
}

void Database::close()
{
    if (m_connName.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, /*open=*/false);
        if (db.isOpen())
            db.close();
    } // local handle destroyed here, before removeDatabase
    QSqlDatabase::removeDatabase(m_connName);
    m_connName.clear();
}

} // namespace klr
