// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemorybindingrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitebindingrepository.h"
#include "sqliteplantrepository.h"
#include "sqlitesensorrepository.h"
#include "storageerror.h"

using namespace klr;

// The in-memory fake and the SQLite impl pass the SAME behavioural suite. The defining
// binding guarantees: a swap is close-then-open, a sensor can be shared by many plants, and
// the per-plant overlap rule rejects conflicting explicit-role bindings identically in
// both impls. (Reading attribution across a swap is exercised in test_repository once
// readings are re-keyed on sensor_id — commit 3.)
namespace {

const QDateTime kT0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);

void checkBindActiveUnbind(IBindingRepository &repo, PlantId plant, SensorId s)
{
    repo.bind(plant, s, kT0, std::nullopt);
    QCOMPARE(repo.activeFor(plant, kT0.addDays(1)).size(), 1);

    repo.unbind(plant, s, kT0.addDays(10));
    QVERIFY(repo.activeFor(plant, kT0.addDays(11)).isEmpty()); // closed
    QCOMPARE(repo.activeFor(plant, kT0.addDays(1)).size(), 1); // still active mid-window

    const QList<PlantSensorBinding> all = repo.bindings(plant);
    QCOMPARE(all.size(), 1);
    QVERIFY(all.first().validTo.has_value());
}

void checkSwapClosesThenOpens(IBindingRepository &repo, PlantId plant, SensorId a, SensorId b)
{
    const QDateTime swap = kT0.addDays(30);
    repo.bind(plant, a, kT0, std::nullopt);
    repo.unbind(plant, a, swap);
    repo.bind(plant, b, swap, std::nullopt);

    const QList<PlantSensorBinding> before = repo.activeFor(plant, kT0.addDays(1));
    QCOMPARE(before.size(), 1);
    QCOMPARE(before.first().sensor, a);

    const QList<PlantSensorBinding> after = repo.activeFor(plant, swap.addDays(1));
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().sensor, b);

    QCOMPARE(repo.bindings(plant).size(), 2); // both edges retained (audited swap)
}

void checkSharedSensorAcrossPlants(IBindingRepository &repo, SensorId shared, PlantId a, PlantId b)
{
    // One probe, two plants in the same pot (many-to-many: sensor -> N plants).
    repo.bind(a, shared, kT0, std::nullopt);
    repo.bind(b, shared, kT0, std::nullopt);

    QCOMPARE(repo.activeFor(a, kT0.addDays(1)).size(), 1);
    QCOMPARE(repo.activeFor(b, kT0.addDays(1)).size(), 1);
    QCOMPARE(repo.activeFor(a, kT0.addDays(1)).first().sensor, shared);

    // Unbinding from A leaves B's binding intact.
    repo.unbind(a, shared, kT0.addDays(5));
    QVERIFY(repo.activeFor(a, kT0.addDays(6)).isEmpty());
    QCOMPARE(repo.activeFor(b, kT0.addDays(6)).size(), 1);
}

void checkRedundantNoRoleAllowed(IBindingRepository &repo, PlantId plant, SensorId a, SensorId b)
{
    repo.bind(plant, a, kT0, std::nullopt);
    repo.bind(plant, b, kT0, std::nullopt); // two soil probes in one big pot — allowed
    QCOMPARE(repo.activeFor(plant, kT0.addDays(1)).size(), 2);
}

void checkOverlappingExplicitRoleRejected(IBindingRepository &repo, PlantId plant, SensorId a,
                                           SensorId b)
{
    repo.bind(plant, a, kT0, Quantity::AirTemperature);
    QVERIFY_EXCEPTION_THROWN(repo.bind(plant, b, kT0.addDays(1), Quantity::AirTemperature),
                             StorageError);
    // The rejected binding was not stored.
    QCOMPARE(repo.bindings(plant).size(), 1);
}

// A sensor shared by two plants: bindingsForSensor sees every edge across plants,
// open or closed; removeForSensor clears them all without touching another sensor's.
void checkBindingsForSensorAndRemove(IBindingRepository &repo, PlantId a, PlantId b,
                                     SensorId shared, SensorId other)
{
    repo.bind(a, shared, kT0, std::nullopt);
    repo.bind(b, shared, kT0, std::nullopt);
    repo.unbind(a, shared, kT0.addDays(5)); // close A's edge (still open for B)
    repo.bind(a, other, kT0, std::nullopt); // a different sensor on plant A

    const QList<PlantSensorBinding> forShared = repo.bindingsForSensor(shared);
    QCOMPARE(forShared.size(), 2); // both edges, across plants A and B
    for (const PlantSensorBinding &edge : forShared)
        QCOMPARE(edge.sensor, shared);
    // Exactly one of the two is still open (B's).
    int open = 0;
    for (const PlantSensorBinding &edge : forShared)
        open += edge.validTo.has_value() ? 0 : 1;
    QCOMPARE(open, 1);

    repo.removeForSensor(shared);
    QVERIFY(repo.bindingsForSensor(shared).isEmpty());
    QCOMPARE(repo.bindingsForSensor(other).size(), 1); // the other sensor's edge survives
}

} // namespace

class TestBindingRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

    // FK-satisfying fixtures for the SQLite impl: a real plant + sensor row.
    PlantId makePlant(Database &db, const QString &name)
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = name;
        p.trackedSince = kT0;
        SqlitePlantRepository(db).add(p);
        return p.id;
    }
    SensorId makeSensor(Database &db, const QString &mac)
    {
        return SqliteSensorRepository(db).ensure(HandleKind::Mac, mac, QStringLiteral("m"));
    }

private slots:
    void bindActiveUnbind()
    {
        { InMemoryBindingRepository r; checkBindActiveUnbind(r, PlantId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkBindActiveUnbind(r, makePlant(db, QStringLiteral("p")), makeSensor(db, QStringLiteral("a"))); }
    }
    void swapClosesThenOpens()
    {
        { InMemoryBindingRepository r;
          checkSwapClosesThenOpens(r, PlantId::generate(), SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkSwapClosesThenOpens(r, makePlant(db, QStringLiteral("p")),
                                   makeSensor(db, QStringLiteral("a")), makeSensor(db, QStringLiteral("b"))); }
    }
    void sharedSensorAcrossPlants()
    {
        { InMemoryBindingRepository r;
          checkSharedSensorAcrossPlants(r, SensorId::generate(), PlantId::generate(), PlantId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkSharedSensorAcrossPlants(r, makeSensor(db, QStringLiteral("s")),
                                        makePlant(db, QStringLiteral("a")), makePlant(db, QStringLiteral("b"))); }
    }
    void redundantNoRoleAllowed()
    {
        { InMemoryBindingRepository r;
          checkRedundantNoRoleAllowed(r, PlantId::generate(), SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkRedundantNoRoleAllowed(r, makePlant(db, QStringLiteral("p")),
                                      makeSensor(db, QStringLiteral("a")), makeSensor(db, QStringLiteral("b"))); }
    }
    void overlappingExplicitRoleRejected()
    {
        { InMemoryBindingRepository r;
          checkOverlappingExplicitRoleRejected(r, PlantId::generate(), SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkOverlappingExplicitRoleRejected(r, makePlant(db, QStringLiteral("p")),
                                               makeSensor(db, QStringLiteral("a")), makeSensor(db, QStringLiteral("b"))); }
    }
    void bindingsForSensorAndRemove()
    {
        { InMemoryBindingRepository r;
          checkBindingsForSensorAndRemove(r, PlantId::generate(), PlantId::generate(),
                                          SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteBindingRepository r(db);
          checkBindingsForSensorAndRemove(r, makePlant(db, QStringLiteral("a")),
                                          makePlant(db, QStringLiteral("b")),
                                          makeSensor(db, QStringLiteral("s")),
                                          makeSensor(db, QStringLiteral("o"))); }
    }
};

QTEST_GUILESS_MAIN(TestBindingRepository)
#include "test_bindingrepository.moc"
