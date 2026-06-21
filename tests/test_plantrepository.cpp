// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemoryplantrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqliteplantrepository.h"

using namespace klr;

// The repository exit criterion: the in-memory fake and the SQLite impl pass the SAME
// behavioural suite. Each scenario below runs its body against both, so the two
// implementations can never silently diverge.
namespace {

Plant makePlant(const QString &name, const QString &species, const QDateTime &since)
{
    Plant p;
    p.id = PlantId::generate();
    p.displayName = name;
    p.species = species;
    p.trackedSince = since;
    return p;
}

void checkAddGetRoundtrip(IPlantRepository &repo)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const Plant ficus = makePlant(QStringLiteral("Ficus"), QStringLiteral("Ficus elastica"), now);
    repo.add(ficus);

    const std::optional<Plant> got = repo.get(ficus.id);
    QVERIFY(got.has_value());
    QCOMPARE(*got, ficus);
}

void checkSensorlessSpeciesless(IPlantRepository &repo)
{
    // A plant with no species (and, implicitly, no sensor) is fully valid (goal #1).
    const Plant bare = makePlant(QStringLiteral("Mystery plant"), QString(),
                                 QDateTime::currentDateTimeUtc());
    repo.add(bare);

    const std::optional<Plant> got = repo.get(bare.id);
    QVERIFY(got.has_value());
    QVERIFY(got->species.isEmpty());
    QCOMPARE(got->displayName, QStringLiteral("Mystery plant"));
}

void checkUpdate(IPlantRepository &repo)
{
    Plant p = makePlant(QStringLiteral("old"), QStringLiteral("x"),
                        QDateTime::currentDateTimeUtc());
    repo.add(p);
    p.displayName = QStringLiteral("new");
    p.species = QStringLiteral("y");
    repo.update(p);

    const std::optional<Plant> got = repo.get(p.id);
    QVERIFY(got.has_value());
    QCOMPARE(got->displayName, QStringLiteral("new"));
    QCOMPARE(got->species, QStringLiteral("y"));
}

void checkRemove(IPlantRepository &repo)
{
    const Plant p = makePlant(QStringLiteral("doomed"), QString(),
                              QDateTime::currentDateTimeUtc());
    repo.add(p);
    QVERIFY(repo.get(p.id).has_value());
    repo.remove(p.id);
    QVERIFY(!repo.get(p.id).has_value());
}

void checkAllOrdered(IPlantRepository &repo)
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    const Plant older = makePlant(QStringLiteral("older"), QString(), t0);
    const Plant newer = makePlant(QStringLiteral("newer"), QString(), t0.addSecs(60));
    repo.add(newer); // add out of order on purpose
    repo.add(older);

    const QList<Plant> all = repo.all();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.first().id, older.id); // ordered by trackedSince
    QCOMPARE(all.last().id, newer.id);
}

} // namespace

class TestPlantRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    // A fresh in-memory SQLite DB, migrated to the baseline.
    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void addGetRoundtrip()
    {
        { InMemoryPlantRepository r; checkAddGetRoundtrip(r); }
        { Database db = freshDb(); SqlitePlantRepository r(db); checkAddGetRoundtrip(r); }
    }
    void sensorlessSpeciesless()
    {
        { InMemoryPlantRepository r; checkSensorlessSpeciesless(r); }
        { Database db = freshDb(); SqlitePlantRepository r(db); checkSensorlessSpeciesless(r); }
    }
    void update()
    {
        { InMemoryPlantRepository r; checkUpdate(r); }
        { Database db = freshDb(); SqlitePlantRepository r(db); checkUpdate(r); }
    }
    void remove()
    {
        { InMemoryPlantRepository r; checkRemove(r); }
        { Database db = freshDb(); SqlitePlantRepository r(db); checkRemove(r); }
    }
    void allOrdered()
    {
        { InMemoryPlantRepository r; checkAllOrdered(r); }
        { Database db = freshDb(); SqlitePlantRepository r(db); checkAllOrdered(r); }
    }
};

QTEST_GUILESS_MAIN(TestPlantRepository)
#include "test_plantrepository.moc"
