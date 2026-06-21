// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemorysyncstaterepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitesensorrepository.h"
#include "sqlitesyncstaterepository.h"

using namespace klr;

// The per-sensor history-sync marker (ADR 0014, schema v4). The in-memory fake and the SQLite impl
// pass the SAME behavioural suite, so they can never silently diverge.
namespace {

void checkRoundTrip(ISyncStateRepository &repo, SensorId id)
{
    QVERIFY(!repo.lastHistorySync(id).has_value()); // never synced

    const QDateTime t1(QDate(2026, 1, 1), QTime(8, 0), QTimeZone::UTC);
    repo.setLastHistorySync(id, t1);
    const auto got = repo.lastHistorySync(id);
    QVERIFY(got.has_value());
    QCOMPARE(*got, t1);

    // Upsert: a later sync replaces the marker.
    const QDateTime t2 = t1.addSecs(6 * 3600);
    repo.setLastHistorySync(id, t2);
    QCOMPARE(*repo.lastHistorySync(id), t2);

    // An unrelated sensor is still unsynced.
    QVERIFY(!repo.lastHistorySync(SensorId::generate()).has_value());
}

} // namespace

class TestSyncStateRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void roundTrips()
    {
        {
            InMemorySyncStateRepository r;
            checkRoundTrip(r, SensorId::generate());
        }
        {
            // The SQLite table FK-references sensors(id), so mint a sensor first.
            Database db = freshDb();
            SqliteSensorRepository sensors(db);
            const SensorId id =
                sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"), QStringLiteral("Flower Care"));
            SqliteSyncStateRepository r(db);
            checkRoundTrip(r, id);
        }
    }

    void v3ToV4MigrationAddsTheTable()
    {
        // Build to the pre-v4 schema, then apply v4 incrementally — the table must then work.
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(3);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(4);

        SqliteSensorRepository sensors(db);
        const SensorId id =
            sensors.ensure(HandleKind::Mac, QStringLiteral("11:22:33:44:55:66"), QStringLiteral("x"));
        SqliteSyncStateRepository r(db);
        const QDateTime t(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::UTC);
        r.setLastHistorySync(id, t);
        QCOMPARE(*r.lastHistorySync(id), t);
    }
};

QTEST_GUILESS_MAIN(TestSyncStateRepository)
#include "test_syncstaterepository.moc"
