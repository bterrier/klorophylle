// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemorysensorrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitesensorrepository.h"

using namespace klr;

// The in-memory fake and the SQLite impl pass the SAME behavioural suite, so the two
// can never silently diverge. The key invariant: `ensure` dedups on the raw handle,
// never assuming a MAC.
namespace {

void checkEnsureMintsThenDedups(ISensorRepository &repo)
{
    const SensorId first = repo.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"),
                                       QStringLiteral("Flower care"));
    // Same handle -> same SensorId, even with a different model string.
    const SensorId again = repo.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"),
                                        QStringLiteral("ignored"));
    QCOMPARE(again, first);
    QCOMPARE(repo.all().size(), 1);

    const std::optional<Sensor> s = repo.get(first);
    QVERIFY(s.has_value());
    QCOMPARE(s->model, QStringLiteral("Flower care")); // first-sight model kept
}

void checkDistinctHandlesAreDistinctSensors(ISensorRepository &repo)
{
    const SensorId mac = repo.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"),
                                     QStringLiteral("m"));
    // Same value but a different handle KIND is a different sensor (Apple UUID).
    const SensorId uuid = repo.ensure(HandleKind::CoreBluetoothUuid,
                                      QStringLiteral("AA:BB:CC:DD:EE:FF"), QStringLiteral("m"));
    QVERIFY(!(mac == uuid));
    QCOMPARE(repo.all().size(), 2);
}

void checkFindByHandle(ISensorRepository &repo)
{
    const SensorId id =
        repo.ensure(HandleKind::Mac, QStringLiteral("12:34:56:78:9A:BC"), QStringLiteral("x"));
    const std::optional<Sensor> hit =
        repo.findByHandle(HandleKind::Mac, QStringLiteral("12:34:56:78:9A:BC"));
    QVERIFY(hit.has_value());
    QCOMPARE(hit->id, id);
    QVERIFY(!repo.findByHandle(HandleKind::Mac, QStringLiteral("no:such")).has_value());
}

void checkRemoveDeletesSensor(ISensorRepository &repo)
{
    const SensorId a = repo.ensure(HandleKind::Mac, QStringLiteral("AA:AA:AA:AA:AA:AA"),
                                   QStringLiteral("m"));
    const SensorId b = repo.ensure(HandleKind::Mac, QStringLiteral("BB:BB:BB:BB:BB:BB"),
                                   QStringLiteral("m"));
    QCOMPARE(repo.all().size(), 2);

    repo.remove(a);
    QVERIFY(!repo.get(a).has_value());
    QCOMPARE(repo.all().size(), 1);
    QCOMPARE(repo.all().first().id, b); // the other sensor is untouched

    repo.remove(a);                     // removing an unknown id is a harmless no-op
    QCOMPARE(repo.all().size(), 1);
}

} // namespace

class TestSensorRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void ensureMintsThenDedups()
    {
        { InMemorySensorRepository r(m_clock); checkEnsureMintsThenDedups(r); }
        { Database db = freshDb(); SqliteSensorRepository r(db); checkEnsureMintsThenDedups(r); }
    }
    void distinctHandlesAreDistinctSensors()
    {
        { InMemorySensorRepository r(m_clock); checkDistinctHandlesAreDistinctSensors(r); }
        { Database db = freshDb(); SqliteSensorRepository r(db); checkDistinctHandlesAreDistinctSensors(r); }
    }
    void findByHandle()
    {
        { InMemorySensorRepository r(m_clock); checkFindByHandle(r); }
        { Database db = freshDb(); SqliteSensorRepository r(db); checkFindByHandle(r); }
    }
    void removeDeletesSensor()
    {
        { InMemorySensorRepository r(m_clock); checkRemoveDeletesSensor(r); }
        { Database db = freshDb(); SqliteSensorRepository r(db); checkRemoveDeletesSensor(r); }
    }
};

QTEST_GUILESS_MAIN(TestSensorRepository)
#include "test_sensorrepository.moc"
