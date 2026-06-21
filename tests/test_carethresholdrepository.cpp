// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h"
#include "clock.h"
#include "database.h"
#include "icarethresholdrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "migrationrunner.h"
#include "plant.h"
#include "schema.h"
#include "sqlitecarethresholdrepository.h"
#include "sqliteplantrepository.h"

#include <array>

using namespace klr;

// The in-memory fake and the SQLite impl pass the SAME behavioural suite (the
// repository-boundary guarantee, ADR 0009): upsert-by-quantity, unset-range deletes,
// replaceAll is atomic, clear empties.
namespace {

CareRange r(Quantity q, std::optional<double> lo, std::optional<double> hi)
{
    return CareRange{ q, lo, hi };
}

std::optional<CareRange> find(const QList<CareRange> &set, Quantity q)
{
    return rangeFor(std::span<const CareRange>(set.constData(), set.size()), q);
}

void checkUpsertAndUnset(ICareThresholdRepository &repo, PlantId p)
{
    repo.setRange(p, r(Quantity::SoilMoisture, 20.0, 60.0));
    repo.setRange(p, r(Quantity::AirTemperature, 15.0, 30.0));
    QCOMPARE(repo.thresholdsFor(p).size(), 2);

    // Upsert the same quantity -> replaces, not appends.
    repo.setRange(p, r(Quantity::SoilMoisture, 25.0, 55.0));
    QCOMPARE(repo.thresholdsFor(p).size(), 2);
    QCOMPARE(find(repo.thresholdsFor(p), Quantity::SoilMoisture)->min, std::optional<double>(25.0));

    // An unset range deletes the row.
    repo.setRange(p, r(Quantity::SoilMoisture, std::nullopt, std::nullopt));
    QCOMPARE(repo.thresholdsFor(p).size(), 1);
    QVERIFY(!find(repo.thresholdsFor(p), Quantity::SoilMoisture).has_value());

    // One-sided bound persists.
    repo.setRange(p, r(Quantity::Illuminance, 1500.0, std::nullopt));
    const auto lux = find(repo.thresholdsFor(p), Quantity::Illuminance);
    QVERIFY(lux.has_value());
    QCOMPARE(lux->min, std::optional<double>(1500.0));
    QVERIFY(!lux->max.has_value());
}

void checkReplaceAllAndClear(ICareThresholdRepository &repo, PlantId p)
{
    repo.setRange(p, r(Quantity::SoilMoisture, 20.0, 60.0));

    const std::array seed{ r(Quantity::AirTemperature, 8.0, 35.0),
                           r(Quantity::AirHumidity, 30.0, 80.0),
                           r(Quantity::SoilConductivity, std::nullopt, std::nullopt) }; // dropped
    repo.replaceAll(p, seed);

    const QList<CareRange> after = repo.thresholdsFor(p);
    QCOMPARE(after.size(), 2); // old soil-moisture gone, unset conductivity not stored
    QVERIFY(find(after, Quantity::AirTemperature).has_value());
    QVERIFY(!find(after, Quantity::SoilMoisture).has_value());

    repo.clear(p);
    QVERIFY(repo.thresholdsFor(p).isEmpty());
}

void checkPerPlantIsolation(ICareThresholdRepository &repo, PlantId a, PlantId b)
{
    repo.setRange(a, r(Quantity::SoilMoisture, 20.0, 60.0));
    repo.setRange(b, r(Quantity::SoilMoisture, 10.0, 40.0));
    repo.clear(a);
    QVERIFY(repo.thresholdsFor(a).isEmpty());
    QCOMPARE(repo.thresholdsFor(b).size(), 1); // b untouched
}

} // namespace

class TestCareThresholdRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

    PlantId makePlant(Database &db, const QString &name)
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = name;
        p.trackedSince = QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        SqlitePlantRepository(db).add(p);
        return p.id;
    }

private slots:
    void upsertAndUnset()
    {
        { InMemoryCareThresholdRepository r; checkUpsertAndUnset(r, PlantId::generate()); }
        { Database db = freshDb(); SqliteCareThresholdRepository r(db);
          checkUpsertAndUnset(r, makePlant(db, QStringLiteral("p"))); }
    }
    void replaceAllAndClear()
    {
        { InMemoryCareThresholdRepository r; checkReplaceAllAndClear(r, PlantId::generate()); }
        { Database db = freshDb(); SqliteCareThresholdRepository r(db);
          checkReplaceAllAndClear(r, makePlant(db, QStringLiteral("p"))); }
    }
    void perPlantIsolation()
    {
        { InMemoryCareThresholdRepository r;
          checkPerPlantIsolation(r, PlantId::generate(), PlantId::generate()); }
        { Database db = freshDb(); SqliteCareThresholdRepository r(db);
          checkPerPlantIsolation(r, makePlant(db, QStringLiteral("a")),
                                 makePlant(db, QStringLiteral("b"))); }
    }
    void cascadeDeleteWithPlant()
    {
        // FK ON DELETE CASCADE: deleting the plant drops its thresholds.
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteCareThresholdRepository thresholds(db);
        const PlantId p = makePlant(db, QStringLiteral("doomed"));
        thresholds.setRange(p, r(Quantity::SoilMoisture, 20.0, 60.0));
        QCOMPARE(thresholds.thresholdsFor(p).size(), 1);
        plants.remove(p);
        QVERIFY(thresholds.thresholdsFor(p).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCareThresholdRepository)
#include "test_carethresholdrepository.moc"
