// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "binding.h"
#include "clock.h"
#include "database.h"
#include "ids.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "migrationrunner.h"
#include "reading.h"
#include "schema.h"
#include "sensor.h"
#include "sqlitereadingrepository.h"
#include "sqlitesensorrepository.h"

#include <QtCore/QTimeZone>
#include <QtSql/QSqlQuery>

using namespace klr;

// The reading repository, exercised against both impls (parity). Readings store
// keyed on the SENSOR; the plant-facing reads derive the plant through the time-
// bounded bindings, so history follows the plant across a swap and a shared sensor
// attributes to every bound plant (ADR 0005). The bindings are passed in as values,
// so these tests need no binding repository.
namespace {

const QDateTime kT0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);

Reading soil(double v, const QDateTime &ts)
{
    return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::Advertisement };
}
Reading temp(double v, const QDateTime &ts)
{
    return { Quantity::AirTemperature, v, Unit::DegreeCelsius, ts, Provenance::Advertisement };
}
PlantSensorBinding edge(PlantId p, SensorId s, const QDateTime &from,
                        std::optional<QDateTime> to = std::nullopt,
                        std::optional<Quantity> role = std::nullopt)
{
    return { p, s, from, to, role };
}

void checkSensorKeyedHistoryAndLatest(IReadingRepository &repo, SensorId a)
{
    const Reading rs[] = { soil(40.0, kT0), soil(38.0, kT0.addSecs(3600)) };
    repo.append(a, rs);

    QCOMPARE(repo.history(a, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addSecs(7200)).size(), 2);
    QCOMPARE(repo.history(a, Quantity::SoilMoisture, kT0.addSecs(1800), kT0.addSecs(7200)).size(), 1);
    // A different quantity is not returned.
    QVERIFY(repo.history(a, Quantity::AirTemperature, kT0.addSecs(-10), kT0.addSecs(7200)).isEmpty());

    const std::optional<Reading> latest = repo.latest(a, Quantity::SoilMoisture);
    QVERIFY(latest.has_value());
    QCOMPARE(latest->value.value(), 38.0); // freshest present value
}

void checkSwapRehomesHistoryToThePlant(IReadingRepository &repo, SensorId a, SensorId b)
{
    const PlantId orchid = PlantId::generate();
    const QDateTime swap = kT0.addDays(30);

    repo.append(a, std::array{ soil(55.0, kT0) });                 // while A is bound
    repo.append(a, std::array{ soil(99.0, kT0.addDays(40)) });     // AFTER A was unbound
    repo.append(b, std::array{ soil(50.0, kT0.addDays(60)) });     // while B is bound

    const PlantSensorBinding bindings[] = {
        edge(orchid, a, kT0, swap), // A bound [t0, swap)
        edge(orchid, b, swap),      // B bound [swap, open)
    };

    const QList<Reading> series =
        repo.seriesForPlant(bindings, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addDays(90));
    // Both A's and B's in-window samples, attributed to the plant; A's post-unbind
    // sample (99) is NOT attributed — A's binding window had closed.
    QCOMPARE(series.size(), 2);
    QCOMPARE(series.first().value.value(), 55.0); // oldest first
    QCOMPARE(series.last().value.value(), 50.0);
}

void checkSharedSensorAttributesToBothPlants(IReadingRepository &repo, SensorId shared)
{
    const PlantId a = PlantId::generate();
    const PlantId b = PlantId::generate();
    repo.append(shared, std::array{ soil(42.0, kT0) }); // stored ONCE

    const PlantSensorBinding toA[] = { edge(a, shared, kT0) };
    const PlantSensorBinding toB[] = { edge(b, shared, kT0) };

    const auto sa = repo.seriesForPlant(toA, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addDays(1));
    const auto sb = repo.seriesForPlant(toB, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addDays(1));
    QCOMPARE(sa.size(), 1);
    QCOMPARE(sb.size(), 1);
    QCOMPARE(sa.first().value.value(), 42.0);
    QCOMPARE(sb.first().value.value(), 42.0); // same sample, two plants
}

void checkCurrentNewestWinsAcrossSensors(IReadingRepository &repo, SensorId a, SensorId b)
{
    const PlantId p = PlantId::generate();
    repo.append(a, std::array{ soil(40.0, kT0) });
    repo.append(b, std::array{ soil(60.0, kT0.addSecs(3600)) }); // fresher

    const PlantSensorBinding bindings[] = { edge(p, a, kT0), edge(p, b, kT0) }; // both no-role
    const QList<Reading> current = repo.currentForPlant(bindings);
    QCOMPARE(current.size(), 1);
    QCOMPARE(current.first().quantity, Quantity::SoilMoisture);
    QCOMPARE(current.first().value.value(), 60.0); // newest-wins
}

void checkBucketingCollapsesAndKeepsLatest(IReadingRepository &repo, SensorId a)
{
    // Three samples inside the SAME hour collapse to one row; the latest present value
    // wins (ADR 0006 append-vs-replace).
    repo.append(a, std::array{ soil(40.0, kT0) });
    repo.append(a, std::array{ soil(41.0, kT0.addSecs(600)) });  // +10 min, same bucket
    repo.append(a, std::array{ soil(42.0, kT0.addSecs(1200)) }); // +20 min, same bucket
    QList<Reading> h = repo.history(a, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addSecs(3600));
    QCOMPARE(h.size(), 1);
    QCOMPARE(h.first().value.value(), 42.0); // latest in the bucket

    // A later ABSENT (NULL) sample in the same bucket must NOT erase the present value.
    repo.append(a, std::array{ Reading{ Quantity::SoilMoisture, std::nullopt, Unit::Percent,
                                         kT0.addSecs(1800), Provenance::Advertisement } });
    h = repo.history(a, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addSecs(3600));
    QCOMPARE(h.size(), 1);
    QCOMPARE(h.first().value.value(), 42.0); // unchanged — absence is not news

    // The next hour is its own bucket.
    repo.append(a, std::array{ soil(30.0, kT0.addSecs(3600)) });
    QCOMPARE(repo.history(a, Quantity::SoilMoisture, kT0.addSecs(-10), kT0.addSecs(7200)).size(), 2);
}

void checkCurrentExplicitRoleBeatsFresher(IReadingRepository &repo, SensorId a, SensorId b)
{
    const PlantId p = PlantId::generate();
    repo.append(a, std::array{ temp(20.0, kT0.addSecs(3600)) }); // no-role sensor, FRESHER
    repo.append(b, std::array{ temp(18.0, kT0) });               // role-pinned sensor, older

    const PlantSensorBinding bindings[] = {
        edge(p, a, kT0),                                    // no role
        edge(p, b, kT0, std::nullopt, Quantity::AirTemperature), // explicit role
    };
    const QList<Reading> current = repo.currentForPlant(bindings);
    QCOMPARE(current.size(), 1);
    QCOMPARE(current.first().value.value(), 18.0); // role-pinned sensor wins despite being older
}

// ISensorRepository::add — the id-preserving insert restore relies on (ADR 0010). Unlike
// ensure() it does NOT mint a new id, so the backup's bindings/readings keyed on the old
// SensorId survive; re-adding the same id updates in place (idempotent restore).
void checkSensorAddPreservesIdAndUpserts(ISensorRepository &repo)
{
    Sensor s;
    s.id = SensorId::generate();
    s.model = QStringLiteral("FlowerCare");
    s.handleKind = HandleKind::Mac;
    s.handleValue = QStringLiteral("AA:BB:CC:DD:EE:FF");
    s.firstSeen = kT0;

    repo.add(s);
    const std::optional<Sensor> got = repo.get(s.id);
    QVERIFY(got.has_value());
    QCOMPARE(*got, s); // every field preserved, id unchanged
    QCOMPARE(repo.all().size(), 1);

    // Re-adding the same id updates in place — no duplicate row.
    Sensor renamed = s;
    renamed.model = QStringLiteral("FlowerCare 2");
    repo.add(renamed);
    QCOMPARE(repo.all().size(), 1);
    QCOMPARE(repo.get(s.id)->model, QStringLiteral("FlowerCare 2"));
}

} // namespace

class TestRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }
    // SQLite readings FK-reference sensors(id), so the row must exist first.
    SensorId makeSensor(Database &db, const QString &mac)
    {
        return SqliteSensorRepository(db).ensure(HandleKind::Mac, mac, QStringLiteral("m"));
    }

private slots:
    void sensorAddPreservesIdAndUpserts()
    {
        { InMemorySensorRepository r(m_clock); checkSensorAddPreservesIdAndUpserts(r); }
        { Database db = freshDb(); SqliteSensorRepository r(db);
          checkSensorAddPreservesIdAndUpserts(r); }
    }
    void sensorKeyedHistoryAndLatest()
    {
        { InMemoryReadingRepository r; checkSensorKeyedHistoryAndLatest(r, SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkSensorKeyedHistoryAndLatest(r, makeSensor(db, QStringLiteral("a"))); }
    }
    void swapRehomesHistoryToThePlant()
    {
        { InMemoryReadingRepository r;
          checkSwapRehomesHistoryToThePlant(r, SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkSwapRehomesHistoryToThePlant(r, makeSensor(db, QStringLiteral("a")),
                                            makeSensor(db, QStringLiteral("b"))); }
    }
    void sharedSensorAttributesToBothPlants()
    {
        { InMemoryReadingRepository r; checkSharedSensorAttributesToBothPlants(r, SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkSharedSensorAttributesToBothPlants(r, makeSensor(db, QStringLiteral("s"))); }
    }
    void currentNewestWinsAcrossSensors()
    {
        { InMemoryReadingRepository r;
          checkCurrentNewestWinsAcrossSensors(r, SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkCurrentNewestWinsAcrossSensors(r, makeSensor(db, QStringLiteral("a")),
                                              makeSensor(db, QStringLiteral("b"))); }
    }
    void currentExplicitRoleBeatsFresher()
    {
        { InMemoryReadingRepository r;
          checkCurrentExplicitRoleBeatsFresher(r, SensorId::generate(), SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkCurrentExplicitRoleBeatsFresher(r, makeSensor(db, QStringLiteral("a")),
                                               makeSensor(db, QStringLiteral("b"))); }
    }
    void bucketingCollapsesAndKeepsLatest()
    {
        { InMemoryReadingRepository r; checkBucketingCollapsesAndKeepsLatest(r, SensorId::generate()); }
        { Database db = freshDb(); SqliteReadingRepository r(db);
          checkBucketingCollapsesAndKeepsLatest(r, makeSensor(db, QStringLiteral("a"))); }
    }
    // observed_by is the sync/probe substrate (goal #4/#5): a live/advertisement write
    // is stamped with this node's replica id; an imported History row is not. Asserted
    // on the SQLite side via a raw read of the column (it is not part of domain Reading).
    void provenanceObservedByStamped()
    {
        Database db = freshDb();
        const SensorId s = makeSensor(db, QStringLiteral("p"));
        SqliteReadingRepository r(db);
        r.append(s, std::array{ soil(50.0, kT0) }); // Advertisement
        r.append(s, std::array{ Reading{ Quantity::AirTemperature, 19.0, Unit::DegreeCelsius,
                                          kT0, Provenance::History } });

        QSqlQuery q(db.handle());
        q.prepare(QStringLiteral("SELECT observed_by FROM readings WHERE sensor_id=:s AND quantity=:q"));
        q.bindValue(QStringLiteral(":s"), s.toString());
        q.bindValue(QStringLiteral(":q"), int(Quantity::SoilMoisture));
        QVERIFY(q.exec() && q.next());
        QCOMPARE(q.value(0).toString(), db.replicaId()); // live write → this node

        q.bindValue(QStringLiteral(":s"), s.toString());
        q.bindValue(QStringLiteral(":q"), int(Quantity::AirTemperature));
        QVERIFY(q.exec() && q.next());
        QVERIFY(q.value(0).toString().isEmpty()); // imported History → not observed here
    }
};

QTEST_GUILESS_MAIN(TestRepository)
#include "test_repository.moc"
