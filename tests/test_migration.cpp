// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QtSql/QSqlQuery>

#include "clock.h"
#include "database.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlsupport.h"
#include "storageerror.h"

using namespace klr;

// Proves the half-applied-migration failure mode is handled: steps are transactional, a
// failed step rolls back and throws, and schema_version never advances past it.
class TestMigration : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    static bool tableExists(Database &db, const QString &name)
    {
        QSqlDatabase d = db.handle();
        QSqlQuery q(d);
        q.prepare(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name = :n"));
        q.bindValue(QStringLiteral(":n"), name);
        return q.exec() && q.next();
    }

private slots:
    void freshDbReachesBaseline()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        QCOMPARE(runner.currentVersion(), 0); // nothing applied yet

        runner.migrateTo(kSchemaVersion);
        QCOMPARE(runner.currentVersion(), kSchemaVersion);
        QVERIFY(tableExists(db, QStringLiteral("plants")));
        QVERIFY(tableExists(db, QStringLiteral("journal_entries")));
        QVERIFY(tableExists(db, QStringLiteral("change_log")));
        QVERIFY(tableExists(db, QStringLiteral("app_meta")));
        // v2: the plant<->sensor join.
        QVERIFY(tableExists(db, QStringLiteral("sensors")));
        QVERIFY(tableExists(db, QStringLiteral("plant_sensor_bindings")));
        QVERIFY(tableExists(db, QStringLiteral("readings")));
        // v3: per-plant care thresholds.
        QVERIFY(tableExists(db, QStringLiteral("care_thresholds")));
        // v4: per-sensor history-sync bookkeeping.
        QVERIFY(tableExists(db, QStringLiteral("sensor_sync_state")));
        // v5: AI-agent transcripts.
        QVERIFY(tableExists(db, QStringLiteral("agent_conversations")));
        QVERIFY(tableExists(db, QStringLiteral("agent_messages")));
        // v8: journal photo attachments.
        QVERIFY(tableExists(db, QStringLiteral("attachments")));
    }

    void v1DbUpgradesToV2NonDestructively()
    {
        // A DB stopped at v1 must reach v2 without losing its v1 tables (no DROP).
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(1);
        QCOMPARE(runner.currentVersion(), 1);
        QVERIFY(tableExists(db, QStringLiteral("plants")));
        QVERIFY(!tableExists(db, QStringLiteral("sensors")));

        runner.migrateTo(2);
        QCOMPARE(runner.currentVersion(), 2);
        QVERIFY(tableExists(db, QStringLiteral("plants")));          // v1 intact
        QVERIFY(tableExists(db, QStringLiteral("plant_sensor_bindings"))); // v2 added
    }

    void v1DataSurvivesUpgradeToVN()
    {
        // The round-trip guarantee with real data: a row written at v1 is still there
        // after migrating up to the current version (no silent DROP).
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(1);

        QSqlQuery ins(db.handle());
        ins.prepare(QStringLiteral("INSERT INTO plants(id, display_name, species, tracked_since) "
                                   "VALUES('p1', 'Fern', '', '2026-01-01T00:00:00.000Z')"));
        QVERIFY(ins.exec());

        runner.migrateTo(kSchemaVersion);

        QSqlQuery sel(db.handle());
        QVERIFY(sel.exec(QStringLiteral("SELECT display_name FROM plants WHERE id='p1'")));
        QVERIFY(sel.next());
        QCOMPARE(sel.value(0).toString(), QStringLiteral("Fern")); // survived the upgrade
    }

    void migrateIsIdempotent()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(kSchemaVersion);
        runner.migrateTo(kSchemaVersion); // no-op, must not throw
        QCOMPARE(runner.currentVersion(), kSchemaVersion);
    }

    void foreignKeysEnabled()
    {
        Database db = Database::openInMemory(m_clock);
        QSqlDatabase d = db.handle();
        QSqlQuery q(d);
        QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_keys")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    void failingStepRollsBackAndThrows()
    {
        Database db = Database::openInMemory(m_clock);

        constexpr int kBad = kSchemaVersion + 1;
        std::vector<Migration> migrations = baselineMigrations();
        migrations.push_back({ kBad, "deliberately-bad", [](QSqlQuery &q) {
            detail::execOrThrow(q, QStringLiteral("CREATE TABLE oops (this is not valid sql"));
        } });

        MigrationRunner runner(db.handle(), std::move(migrations));
        QVERIFY_EXCEPTION_THROWN(runner.migrateTo(kBad), StorageError);

        // The baseline committed cleanly; the bad step rolled back — version stays at
        // the baseline, no partial table.
        QCOMPARE(runner.currentVersion(), kSchemaVersion);
        QVERIFY(tableExists(db, QStringLiteral("plants")));
        QVERIFY(!tableExists(db, QStringLiteral("oops")));
    }

    void v5DbUpgradesToV6AddsEditedColumn()
    {
        // ADR 0020: v6 adds journal_entries.ts_edited (nullable). A row written at v5 must
        // survive the upgrade and read the new column back as NULL ("never edited").
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(5);
        QCOMPARE(runner.currentVersion(), 5);

        // At v5 the column does not exist yet — selecting it must fail to prepare/exec.
        {
            QSqlQuery probe(db.handle());
            QVERIFY(!probe.exec(QStringLiteral("SELECT ts_edited FROM journal_entries")));
        }

        // Seed a v5-era plant + journal entry (no ts_edited column to write).
        QSqlQuery insPlant(db.handle());
        insPlant.prepare(QStringLiteral("INSERT INTO plants(id, display_name, species, tracked_since) "
                                        "VALUES('p1', 'Fern', '', '2026-01-01T00:00:00.000Z')"));
        QVERIFY(insPlant.exec());
        QSqlQuery insEntry(db.handle());
        insEntry.prepare(QStringLiteral("INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note) "
                                        "VALUES('e1', 'p1', '2026-01-02T00:00:00.000Z', 0, 'hi')"));
        QVERIFY(insEntry.exec());

        runner.migrateTo(6);
        QCOMPARE(runner.currentVersion(), 6);

        // The column now exists and the pre-existing row reads back NULL.
        QSqlQuery sel(db.handle());
        QVERIFY(sel.exec(QStringLiteral("SELECT ts_edited, note FROM journal_entries WHERE id='e1'")));
        QVERIFY(sel.next());
        QVERIFY(sel.value(0).isNull());                          // never edited
        QCOMPARE(sel.value(1).toString(), QStringLiteral("hi")); // v5 data survived
    }

    void v6DbUpgradesToV7AllowsNullPlant()
    {
        // ADR 0022: v7 rebuilds journal_entries so plant_id is NULLABLE (global entries). A v6 row
        // must survive the rebuild verbatim (id, plant, note, ts_edited), and a NULL-plant insert —
        // rejected at v6's NOT NULL — must succeed afterwards.
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(6);
        QCOMPARE(runner.currentVersion(), 6);

        QSqlQuery insPlant(db.handle());
        insPlant.prepare(QStringLiteral("INSERT INTO plants(id, display_name, species, tracked_since) "
                                        "VALUES('p1', 'Fern', '', '2026-01-01T00:00:00.000Z')"));
        QVERIFY(insPlant.exec());
        QSqlQuery insEntry(db.handle());
        insEntry.prepare(QStringLiteral(
            "INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note, ts_edited) "
            "VALUES('e1', 'p1', '2026-01-02T00:00:00.000Z', 0, 'hi', '2026-01-03T00:00:00.000Z')"));
        QVERIFY(insEntry.exec());

        // At v6 a NULL plant_id is rejected by NOT NULL.
        {
            QSqlQuery badGlobal(db.handle());
            QVERIFY(!badGlobal.exec(QStringLiteral(
                "INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note) "
                "VALUES('g0', NULL, '2026-01-04T00:00:00.000Z', 6, 'nope')")));
        }

        runner.migrateTo(7);
        QCOMPARE(runner.currentVersion(), 7);

        // The v6 row survived the rebuild verbatim.
        QSqlQuery sel(db.handle());
        QVERIFY(sel.exec(QStringLiteral(
            "SELECT plant_id, note, ts_edited FROM journal_entries WHERE id='e1'")));
        QVERIFY(sel.next());
        QCOMPARE(sel.value(0).toString(), QStringLiteral("p1"));
        QCOMPARE(sel.value(1).toString(), QStringLiteral("hi"));
        QCOMPARE(sel.value(2).toString(), QStringLiteral("2026-01-03T00:00:00.000Z"));

        // A NULL plant_id (global entry) now inserts cleanly.
        QSqlQuery global(db.handle());
        QVERIFY(global.exec(QStringLiteral(
            "INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note) "
            "VALUES('g1', NULL, '2026-01-05T00:00:00.000Z', 6, 'global memory')")));
        QSqlQuery selG(db.handle());
        QVERIFY(selG.exec(QStringLiteral(
            "SELECT plant_id FROM journal_entries WHERE id='g1'")));
        QVERIFY(selG.next());
        QVERIFY(selG.value(0).isNull());
    }

    void v7DbUpgradesToV8AddsAttachments()
    {
        // ADR 0024: v8 adds the attachments table (FK→journal_entries ON DELETE CASCADE). A v7 row
        // must survive the upgrade, the new table must not exist before it, and deleting the parent
        // entry must cascade-delete its attachment rows.
        Database db = Database::openInMemory(m_clock);
        MigrationRunner runner(db.handle(), baselineMigrations());
        runner.migrateTo(7);
        QCOMPARE(runner.currentVersion(), 7);
        QVERIFY(!tableExists(db, QStringLiteral("attachments"))); // not yet at v7

        QSqlQuery insPlant(db.handle());
        insPlant.prepare(QStringLiteral("INSERT INTO plants(id, display_name, species, tracked_since) "
                                        "VALUES('p1', 'Fern', '', '2026-01-01T00:00:00.000Z')"));
        QVERIFY(insPlant.exec());
        QSqlQuery insEntry(db.handle());
        insEntry.prepare(QStringLiteral("INSERT INTO journal_entries(id, plant_id, ts_utc, kind, note) "
                                        "VALUES('e1', 'p1', '2026-01-02T00:00:00.000Z', 5, 'leaf')"));
        QVERIFY(insEntry.exec());

        runner.migrateTo(8);
        QCOMPARE(runner.currentVersion(), 8);
        QVERIFY(tableExists(db, QStringLiteral("attachments"))); // v8 added
        // The v7 journal row survived the upgrade (no DROP).
        QSqlQuery sel(db.handle());
        QVERIFY(sel.exec(QStringLiteral("SELECT note FROM journal_entries WHERE id='e1'")));
        QVERIFY(sel.next());
        QCOMPARE(sel.value(0).toString(), QStringLiteral("leaf"));

        // An attachment row deletes when its entry is deleted (FK cascade, foreign_keys=ON).
        QSqlQuery insAtt(db.handle());
        insAtt.prepare(QStringLiteral(
            "INSERT INTO attachments(id, entry_id, file_ref, caption, added_at) "
            "VALUES('a1', 'e1', 'attachments/a1.jpg', '', '2026-01-02T01:00:00.000Z')"));
        QVERIFY(insAtt.exec());
        QSqlQuery delEntry(db.handle());
        QVERIFY(delEntry.exec(QStringLiteral("DELETE FROM journal_entries WHERE id='e1'")));
        QSqlQuery cnt(db.handle());
        QVERIFY(cnt.exec(QStringLiteral("SELECT COUNT(*) FROM attachments WHERE id='a1'")));
        QVERIFY(cnt.next());
        QCOMPARE(cnt.value(0).toInt(), 0); // cascaded away
    }
};

QTEST_GUILESS_MAIN(TestMigration)
#include "test_migration.moc"
