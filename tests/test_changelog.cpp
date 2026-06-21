// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QtSql/QSqlQuery>

#include "clock.h"
#include "database.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitejournalrepository.h"
#include "sqliteplantrepository.h"
#include "sqlitesensorrepository.h"
#include "storageerror.h"

using namespace klr;

// The sync-ready substrate: every SQLite mutation appends exactly one change_log
// row, in the same transaction as the mutation (so a failed mutation logs nothing).
class TestChangeLog : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

    static int changeLogCount(Database &db)
    {
        QSqlDatabase d = db.handle();
        QSqlQuery q(d);
        q.exec(QStringLiteral("SELECT COUNT(*) FROM change_log"));
        return q.next() ? q.value(0).toInt() : -1;
    }

    static QString lastOp(Database &db, QString *entity = nullptr, QString *entityId = nullptr)
    {
        QSqlDatabase d = db.handle();
        QSqlQuery q(d);
        q.exec(QStringLiteral(
            "SELECT entity, entity_id, op FROM change_log ORDER BY seq DESC LIMIT 1"));
        if (!q.next())
            return {};
        if (entity)
            *entity = q.value(0).toString();
        if (entityId)
            *entityId = q.value(1).toString();
        return q.value(2).toString();
    }

    static Plant makePlant()
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("Aloe");
        p.trackedSince = QDateTime::currentDateTimeUtc();
        return p;
    }

private slots:
    void plantMutationsAreLogged()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);

        Plant p = makePlant();
        plants.add(p);
        QString entity, id;
        QCOMPARE(changeLogCount(db), 1);
        QCOMPARE(lastOp(db, &entity, &id), QStringLiteral("insert"));
        QCOMPARE(entity, QStringLiteral("plant"));
        QCOMPARE(id, p.id.toString());

        p.displayName = QStringLiteral("Aloe vera");
        plants.update(p);
        QCOMPARE(changeLogCount(db), 2);
        QCOMPARE(lastOp(db), QStringLiteral("update"));

        plants.remove(p.id);
        QCOMPARE(changeLogCount(db), 3);
        QCOMPARE(lastOp(db), QStringLiteral("delete"));
    }

    void sensorDeleteIsLogged()
    {
        Database db = freshDb();
        SqliteSensorRepository sensors(db);

        const SensorId id = sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"),
                                            QStringLiteral("Flower care"));
        QCOMPARE(changeLogCount(db), 1); // the ensure insert

        QString entity, loggedId;
        sensors.remove(id);
        QCOMPARE(changeLogCount(db), 2);
        QCOMPARE(lastOp(db, &entity, &loggedId), QStringLiteral("delete"));
        QCOMPARE(entity, QStringLiteral("sensor"));
        QCOMPARE(loggedId, id.toString());
    }

    void journalMutationIsLogged()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteJournalRepository journal(db);

        const Plant p = makePlant();
        plants.add(p); // parent plant (FK) — also logs one change_log row

        JournalEntry e;
        e.id = JournalEntryId::generate();
        e.plant = p.id;
        e.timestamp = QDateTime::currentDateTimeUtc();
        e.kind = JournalEntryKind::Watering;
        journal.add(e);

        QString entity;
        QCOMPARE(changeLogCount(db), 2); // plant insert + journal insert
        QCOMPARE(lastOp(db, &entity), QStringLiteral("insert"));
        QCOMPARE(entity, QStringLiteral("journal_entry"));
    }

    // A mutation that fails writes neither the row nor a change_log entry.
    void failedMutationLogsNothing()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);

        const Plant p = makePlant();
        plants.add(p);
        QCOMPARE(changeLogCount(db), 1);

        // Re-adding the same id violates the primary key -> the whole transaction
        // (mutation + change_log) rolls back.
        QVERIFY_EXCEPTION_THROWN(plants.add(p), StorageError);
        QCOMPARE(changeLogCount(db), 1); // unchanged
        QCOMPARE(plants.all().size(), 1); // still just the one plant
    }
};

QTEST_GUILESS_MAIN(TestChangeLog)
#include "test_changelog.moc"
