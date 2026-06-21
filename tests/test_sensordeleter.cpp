// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemorybindingrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "migrationrunner.h"
#include "plant.h"
#include "reading.h"
#include "schema.h"
#include "sensordeleter.h"
#include "sqlitebindingrepository.h"
#include "sqliteplantrepository.h"
#include "sqlitereadingrepository.h"
#include "sqlitesensorrepository.h"

#include <span>

using namespace klr;

// SensorDeleter (data hygiene): delete a registered sensor + all its data, but only
// when it is bound to NO plant right now (an open binding means a live plant still uses
// it). Pure orchestration over the repository interfaces, so the SAME behavioural suite
// runs against the in-memory fakes AND the SQLite repos — the deletion converges (the
// SQLite ON DELETE CASCADE vs the use-case's explicit cleanup) and never diverges.
namespace {

const QDateTime kT0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);

Reading soil(double v, const QDateTime &ts)
{
    return Reading{ Quantity::SoilMoisture, v, canonicalUnit(Quantity::SoilMoisture), ts,
                    Provenance::History };
}

void seedReadings(IReadingRepository &readings, SensorId s)
{
    const Reading r[] = { soil(40.0, kT0.addDays(1)), soil(45.0, kT0.addDays(2)) };
    readings.append(s, std::span<const Reading>(r, std::size(r)));
}

// An ORPHAN sensor — no binding references it (e.g. its last plant was deleted, which
// cascade-removed the binding) — can be deleted; its data is gone afterwards and a second,
// untouched sensor keeps its own readings.
void checkDeletesOrphanSensor(ISensorRepository &sensors, IBindingRepository &bindings,
                              IReadingRepository &readings, SensorId target, SensorId other)
{
    seedReadings(readings, target);
    seedReadings(readings, other);
    QVERIFY(bindings.bindingsForSensor(target).isEmpty()); // no plant references it

    SensorDeleter deleter(sensors, bindings, readings);
    const auto r = deleter.remove(target);
    QVERIFY(r.has_value()); // succeeded

    QVERIFY(!sensors.get(target).has_value()); // sensor row gone
    QVERIFY(readings.history(target, Quantity::SoilMoisture, kT0, kT0.addDays(9)).isEmpty());

    // The other sensor is entirely untouched.
    QVERIFY(sensors.get(other).has_value());
    QCOMPARE(readings.history(other, Quantity::SoilMoisture, kT0, kT0.addDays(9)).size(), 2);
}

// A sensor a plant references is refused — both while OPEN-bound AND after detaching (which
// only closes the binding; the plant still owns the history). Data is left intact.
void checkRefusesWhileAnyBinding(ISensorRepository &sensors, IBindingRepository &bindings,
                                 IReadingRepository &readings, PlantId plant, SensorId target)
{
    bindings.bind(plant, target, kT0, std::nullopt); // open
    seedReadings(readings, target);

    SensorDeleter deleter(sensors, bindings, readings);
    QCOMPARE(deleter.remove(target).error(), SensorDeleteError::StillBound); // open: refused

    bindings.unbind(plant, target, kT0.addDays(3)); // detach -> closed binding remains
    QCOMPARE(deleter.remove(target).error(), SensorDeleteError::StillBound); // closed: still refused

    QVERIFY(sensors.get(target).has_value()); // untouched throughout
    QCOMPARE(readings.history(target, Quantity::SoilMoisture, kT0, kT0.addDays(9)).size(), 2);
    QCOMPARE(bindings.bindingsForSensor(target).size(), 1);
}

} // namespace

class TestSensorDeleter : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

    PlantId makePlant(Database &db)
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("p");
        p.trackedSince = kT0;
        SqlitePlantRepository(db).add(p);
        return p.id;
    }

private slots:
    void deletesOrphanSensor()
    {
        {
            InMemorySensorRepository sensors(m_clock);
            InMemoryBindingRepository bindings;
            InMemoryReadingRepository readings;
            const SensorId t = sensors.ensure(HandleKind::Mac, QStringLiteral("a"), QStringLiteral("m"));
            const SensorId o = sensors.ensure(HandleKind::Mac, QStringLiteral("b"), QStringLiteral("m"));
            checkDeletesOrphanSensor(sensors, bindings, readings, t, o);
        }
        {
            Database db = freshDb();
            SqliteSensorRepository sensors(db);
            SqliteBindingRepository bindings(db);
            SqliteReadingRepository readings(db);
            const SensorId t = sensors.ensure(HandleKind::Mac, QStringLiteral("a"), QStringLiteral("m"));
            const SensorId o = sensors.ensure(HandleKind::Mac, QStringLiteral("b"), QStringLiteral("m"));
            checkDeletesOrphanSensor(sensors, bindings, readings, t, o);
        }
    }

    void refusesWhileAnyBinding()
    {
        {
            InMemorySensorRepository sensors(m_clock);
            InMemoryBindingRepository bindings;
            InMemoryReadingRepository readings;
            const SensorId t = sensors.ensure(HandleKind::Mac, QStringLiteral("a"), QStringLiteral("m"));
            checkRefusesWhileAnyBinding(sensors, bindings, readings, PlantId::generate(), t);
        }
        {
            Database db = freshDb();
            SqliteSensorRepository sensors(db);
            SqliteBindingRepository bindings(db);
            SqliteReadingRepository readings(db);
            const SensorId t = sensors.ensure(HandleKind::Mac, QStringLiteral("a"), QStringLiteral("m"));
            checkRefusesWhileAnyBinding(sensors, bindings, readings, makePlant(db), t);
        }
    }

    void unknownSensorReturnsNotFound()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorDeleter deleter(sensors, bindings, readings);
        const auto r = deleter.remove(SensorId::generate());
        QVERIFY(!r.has_value());
        QCOMPARE(r.error(), SensorDeleteError::NotFound);
    }

    // A shared sensor open-bound to a SECOND plant is refused even if detached from the
    // first — any open binding blocks deletion, protecting the still-bound plant's data.
    void sharedSensorWithOneOpenBindingIsRefused()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        const SensorId shared = sensors.ensure(HandleKind::Mac, QStringLiteral("s"), QStringLiteral("m"));
        const PlantId a = PlantId::generate();
        const PlantId b = PlantId::generate();
        bindings.bind(a, shared, kT0, std::nullopt);
        bindings.bind(b, shared, kT0, std::nullopt);
        bindings.unbind(a, shared, kT0.addDays(5)); // detached from A; still open for B

        SensorDeleter deleter(sensors, bindings, readings);
        const auto r = deleter.remove(shared);
        QVERIFY(!r.has_value());
        QCOMPARE(r.error(), SensorDeleteError::StillBound);
        QVERIFY(sensors.get(shared).has_value());
    }
};

QTEST_GUILESS_MAIN(TestSensorDeleter)
#include "test_sensordeleter.moc"
