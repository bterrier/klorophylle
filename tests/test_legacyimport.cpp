// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QtCore/QTemporaryDir>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

#include "clock.h"
#include "database.h"
#include "ibindingrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "legacyimporter.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitebindingrepository.h"
#include "sqlitejournalrepository.h"
#include "sqliteplantrepository.h"
#include "sqlitereadingrepository.h"
#include "sqlitesensorrepository.h"
#include "storageerror.h"

using namespace klr;

// LegacyImporter: maps an existing WatchFlower data.db into the new schema. The fixture
// is a real old-schema SQLite file built in a temp dir, so this exercises the actual
// un-pivot / -99-drop / binding-synthesis path. See ADR 0006.
class TestLegacyImport : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    // Build a minimal but representative legacy data.db at `path`.
    static void writeFixture(const QString &path)
    {
        const QString conn = QStringLiteral("legacy-fixture");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            QVERIFY(db.open());

            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE devices (deviceAddr TEXT PRIMARY KEY, deviceAddrMAC TEXT, "
                "deviceName TEXT, deviceModel TEXT, deviceFirmware TEXT, deviceBattery INT, "
                "associatedName TEXT, locationName TEXT, lastSeen DATETIME, lastSync DATETIME, "
                "isEnabled INT, isOutside INT, manualOrderIndex INT, settings TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plants (plantId INTEGER PRIMARY KEY AUTOINCREMENT, plantName TEXT, "
                "plantCache TEXT, plantStart DATETIME, deviceAddr TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plantJournal (entryId INTEGER PRIMARY KEY AUTOINCREMENT, "
                "entryType INT, entryTimestamp DATETIME, entryComment TEXT, plantId INT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plantData (deviceAddr TEXT, timestamp_rounded DATETIME, "
                "timestamp DATETIME, soilMoisture INT, soilConductivity INT, soilTemperature FLOAT, "
                "soilPH FLOAT, temperature FLOAT, humidity FLOAT, luminosity INT, watertank FLOAT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE thermoData (deviceAddr TEXT, timestamp_rounded DATETIME, "
                "timestamp DATETIME, temperature FLOAT, humidity FLOAT, pressure FLOAT)")));

            // A Flower Care: the user named it "Kitchen orchid" (devices.associatedName),
            // and assigned the species "Phalaenopsis" (plants.plantName + plantCache), plus
            // a journal watering. Name and species are deliberately DIFFERENT so the mapping
            // (associatedName -> displayName, plantName -> species) is unambiguous.
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO devices(deviceAddr, deviceAddrMAC, deviceName, deviceModel, associatedName) "
                "VALUES('AA:BB:CC', 'AA:BB:CC', 'Flower care', 'Flower care', 'Kitchen orchid')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plants(plantId, plantName, plantCache, plantStart, deviceAddr) "
                "VALUES(1, 'Phalaenopsis', '{\"name\":\"Phalaenopsis\",\"name_common\":\"Moth orchid\"}', "
                "'2026-01-01T00:00:00.000', 'AA:BB:CC')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plantJournal(entryType, entryTimestamp, entryComment, plantId) "
                "VALUES(1, '2026-01-02T09:00:00.000', 'watered well', 1)"))); // 1 = WATER

            // Two samples in the SAME hour (collapse to one bucket, latest wins), plus a
            // -99 soilMoisture that must be dropped, not stored.
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plantData(deviceAddr, timestamp_rounded, timestamp, soilMoisture, "
                "soilConductivity, soilTemperature, soilPH, temperature, humidity, luminosity, watertank) "
                "VALUES('AA:BB:CC', '2026-01-03T10:00:00.000', '2026-01-03T10:05:00.000', "
                "40, 800, 19.0, -99, 21.0, 55.0, 1200, -99)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plantData(deviceAddr, timestamp_rounded, timestamp, soilMoisture, "
                "soilConductivity, soilTemperature, soilPH, temperature, humidity, luminosity, watertank) "
                "VALUES('AA:BB:CC', '2026-01-03T10:00:00.000', '2026-01-03T10:40:00.000', "
                "42, 810, 19.2, -99, 21.5, 54.0, 1300, -99)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plantData(deviceAddr, timestamp_rounded, timestamp, soilMoisture, "
                "soilConductivity, soilTemperature, soilPH, temperature, humidity, luminosity, watertank) "
                "VALUES('AA:BB:CC', '2026-01-03T11:00:00.000', '2026-01-03T11:05:00.000', "
                "-99, 820, 19.4, -99, 22.0, 53.0, 1100, -99)"))); // soilMoisture -99 → dropped

            // A bare thermometer with no plants row → still becomes a plant (one per device).
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO devices(deviceAddr, deviceAddrMAC, deviceName, deviceModel, associatedName) "
                "VALUES('DD:EE:FF', 'DD:EE:FF', 'ThermoBeacon', 'ThermoBeacon', 'Living room')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO thermoData(deviceAddr, timestamp_rounded, timestamp, temperature, humidity, pressure) "
                "VALUES('DD:EE:FF', '2026-01-03T10:00:00.000', '2026-01-03T10:05:00.000', 20.0, 45.0, 1013.0)")));
        }
        QSqlDatabase::removeDatabase(conn);
    }

    struct Target {
        Database db;
        SqlitePlantRepository plants;
        SqliteSensorRepository sensors;
        SqliteBindingRepository bindings;
        SqliteReadingRepository readings;
        SqliteJournalRepository journal;
        explicit Target(Database &&d)
            : db(std::move(d)), plants(db), sensors(db), bindings(db), readings(db), journal(db)
        {
        }
    };

    Database freshTargetDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void importsDevicesPlantsJournalAndReadings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        writeFixture(legacy);

        Target t(freshTargetDb());
        LegacyImporter importer(t.plants, t.sensors, t.bindings, t.readings, t.journal, m_clock);
        const LegacyImporter::Result r = importer.importFrom(legacy);

        // Two devices → two sensors + two plants + two bindings.
        QCOMPARE(r.sensors, 2);
        QCOMPARE(r.plants, 2);
        QCOMPARE(r.bindings, 2);
        QCOMPARE(r.journalEntries, 1);
        QVERIFY(r.readings > 0);

        // Both plants carry the USER's name (devices.associatedName), not the species.
        const QList<Plant> plants = t.plants.all();
        QCOMPARE(plants.size(), 2);
        QStringList names;
        for (const Plant &p : plants)
            names << p.displayName;
        QVERIFY(names.contains(QStringLiteral("Kitchen orchid")));
        QVERIFY(names.contains(QStringLiteral("Living room")));

        // The Flower Care plant got its species from plants.plantName; the bare
        // thermometer (no plants row) has none.
        for (const Plant &p : plants) {
            if (p.displayName == QStringLiteral("Kitchen orchid"))
                QCOMPARE(p.species, QStringLiteral("Phalaenopsis"));
            else if (p.displayName == QStringLiteral("Living room"))
                QVERIFY(p.species.isEmpty());
        }

        // The watering journal entry mapped to the Watering kind under the orchid.
        PlantId orchid;
        for (const Plant &p : plants)
            if (p.displayName == QStringLiteral("Kitchen orchid"))
                orchid = p.id;
        const QList<JournalEntry> j = t.journal.forPlant(orchid);
        QCOMPARE(j.size(), 1);
        QCOMPARE(j.first().kind, JournalEntryKind::Watering);
        QCOMPARE(j.first().note, QStringLiteral("watered well"));
    }

    void mapsAllLegacyJournalTypes()
    {
        // Every legacy plantJournal.entryType must land on the right JournalEntryKind, and the
        // comment must survive as the note. In particular: legacy PHOTO (7) was a stub with no
        // path/blob column, so there is nothing to lift — it imports as an Observation that keeps
        // its comment. ROTATE (4) / MOVE (5) have no
        // dedicated kind and fall through to the generic Note, as do COMMENT (8), UNKNOWN (0), and
        // any out-of-range value.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        const QString conn = QStringLiteral("legacy-journal-fixture");

        // Each row's note is unique so assertions key on it, not on list position (forPlant is
        // newest-first). Timestamps strictly increase to keep ordering deterministic.
        const QList<std::pair<int, QString>> rows = {
            { 0, QStringLiteral("t0-unknown") },
            { 1, QStringLiteral("t1-water") },
            { 2, QStringLiteral("t2-fertilize") },
            { 3, QStringLiteral("t3-prune") },
            { 4, QStringLiteral("t4-rotate") },
            { 5, QStringLiteral("t5-move") },
            { 6, QStringLiteral("t6-repot") },
            { 7, QStringLiteral("t7-photo") },
            { 8, QStringLiteral("t8-comment") },
            { 99, QStringLiteral("t99-out-of-range") },
        };
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(legacy);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE devices (deviceAddr TEXT PRIMARY KEY, deviceAddrMAC TEXT, "
                "deviceName TEXT, deviceModel TEXT, associatedName TEXT, lastSeen DATETIME)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plants (plantId INTEGER PRIMARY KEY AUTOINCREMENT, plantName TEXT, "
                "plantCache TEXT, plantStart DATETIME, deviceAddr TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plantJournal (entryId INTEGER PRIMARY KEY AUTOINCREMENT, "
                "entryType INT, entryTimestamp DATETIME, entryComment TEXT, plantId INT)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO devices(deviceAddr, deviceAddrMAC, deviceName, deviceModel, associatedName) "
                "VALUES('AA:BB:CC', 'AA:BB:CC', 'Flower care', 'Flower care', 'Kitchen orchid')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plants(plantId, plantName, plantCache, plantStart, deviceAddr) "
                "VALUES(1, 'Phalaenopsis', '{\"name\":\"Phalaenopsis\"}', '2026-01-01T00:00:00.000', "
                "'AA:BB:CC')")));
            int minute = 0;
            for (const auto &[type, note] : rows) {
                q.prepare(QStringLiteral(
                    "INSERT INTO plantJournal(entryType, entryTimestamp, entryComment, plantId) "
                    "VALUES(:type, :ts, :note, 1)"));
                q.bindValue(QStringLiteral(":type"), type);
                q.bindValue(QStringLiteral(":ts"),
                            QStringLiteral("2026-01-02T09:%1:00.000").arg(minute++, 2, 10, QChar('0')));
                q.bindValue(QStringLiteral(":note"), note);
                QVERIFY(q.exec());
            }
        }
        QSqlDatabase::removeDatabase(conn);

        Target t(freshTargetDb());
        const LegacyImporter::Result r =
            LegacyImporter(t.plants, t.sensors, t.bindings, t.readings, t.journal, m_clock)
                .importFrom(legacy);
        QCOMPARE(r.journalEntries, rows.size());

        const QList<Plant> plants = t.plants.all();
        QCOMPARE(plants.size(), 1);
        const QList<JournalEntry> j = t.journal.forPlant(plants.first().id);
        QCOMPARE(j.size(), rows.size());

        QHash<QString, JournalEntryKind> kindByNote;
        for (const JournalEntry &e : j)
            kindByNote.insert(e.note, e.kind);

        QCOMPARE(kindByNote.value(QStringLiteral("t0-unknown")), JournalEntryKind::Note);
        QCOMPARE(kindByNote.value(QStringLiteral("t1-water")), JournalEntryKind::Watering);
        QCOMPARE(kindByNote.value(QStringLiteral("t2-fertilize")), JournalEntryKind::Fertilizing);
        QCOMPARE(kindByNote.value(QStringLiteral("t3-prune")), JournalEntryKind::Pruning);
        QCOMPARE(kindByNote.value(QStringLiteral("t4-rotate")), JournalEntryKind::Note);
        QCOMPARE(kindByNote.value(QStringLiteral("t5-move")), JournalEntryKind::Note);
        QCOMPARE(kindByNote.value(QStringLiteral("t6-repot")), JournalEntryKind::Repotting);
        QCOMPARE(kindByNote.value(QStringLiteral("t8-comment")), JournalEntryKind::Note);
        QCOMPARE(kindByNote.value(QStringLiteral("t99-out-of-range")), JournalEntryKind::Note);

        // PHOTO: a stub with nothing to lift — Observation with its comment preserved as the note.
        QVERIFY(kindByNote.contains(QStringLiteral("t7-photo")));
        QCOMPARE(kindByNote.value(QStringLiteral("t7-photo")), JournalEntryKind::Observation);
    }

    void unpivotsAndDropsSentinel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        writeFixture(legacy);

        Target t(freshTargetDb());
        LegacyImporter(t.plants, t.sensors, t.bindings, t.readings, t.journal, m_clock)
            .importFrom(legacy);

        // Find the Flower Care sensor (model "Flower care").
        SensorId fc;
        for (const Sensor &s : t.sensors.all())
            if (s.model == QStringLiteral("Flower care"))
                fc = s.id;

        const QDateTime from(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const QDateTime to(QDate(2027, 1, 1), QTime(0, 0), QTimeZone::UTC);

        // Soil moisture: the two 10:00 samples collapsed to one bucket (latest = 42); the
        // 11:00 row was -99 → dropped, so no second bucket exists.
        const QList<Reading> soil = t.readings.history(fc, Quantity::SoilMoisture, from, to);
        QCOMPARE(soil.size(), 1);
        QCOMPARE(soil.first().value.value(), 42.0);

        // Air temperature un-pivoted from the same wide rows: two hourly buckets present.
        const QList<Reading> temp = t.readings.history(fc, Quantity::AirTemperature, from, to);
        QCOMPARE(temp.size(), 2);

        // soilPH has no klr Quantity and -99 anyway → never stored as any quantity.
        for (const Reading &rd : soil)
            QVERIFY(rd.value.has_value()); // no sentinel leaked through as a value
    }

    void speciesFallsBackToPlantCacheWhenPlantNameEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        const QString conn = QStringLiteral("legacy-cache-fixture");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(legacy);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE devices (deviceAddr TEXT PRIMARY KEY, deviceAddrMAC TEXT, "
                "deviceName TEXT, deviceModel TEXT, associatedName TEXT, lastSeen DATETIME)")));
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE plants (plantId INTEGER PRIMARY KEY AUTOINCREMENT, plantName TEXT, "
                "plantCache TEXT, plantStart DATETIME, deviceAddr TEXT)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO devices(deviceAddr, deviceAddrMAC, deviceName, deviceModel, associatedName) "
                "VALUES('11:22:33', '11:22:33', 'Flower care', 'Flower care', 'Balcony fern')")));
            // plantName empty, but the cache snapshot still carries the botanical name.
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plants(plantId, plantName, plantCache, plantStart, deviceAddr) "
                "VALUES(1, '', '{\"name\":\"Nephrolepis exaltata\"}', '2026-01-01T00:00:00.000', "
                "'11:22:33')")));
        }
        QSqlDatabase::removeDatabase(conn);

        Target t(freshTargetDb());
        LegacyImporter(t.plants, t.sensors, t.bindings, t.readings, t.journal, m_clock)
            .importFrom(legacy);

        const QList<Plant> plants = t.plants.all();
        QCOMPARE(plants.size(), 1);
        QCOMPARE(plants.first().displayName, QStringLiteral("Balcony fern"));
        QCOMPARE(plants.first().species, QStringLiteral("Nephrolepis exaltata"));
    }

    void rejectsNonWatchFlowerDb()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString bogus = dir.filePath(QStringLiteral("bogus.db"));
        {
            const QString conn = QStringLiteral("bogus-fixture");
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(bogus);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("CREATE TABLE notes (id INTEGER)"));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("bogus-fixture"));

        Target t(freshTargetDb());
        LegacyImporter importer(t.plants, t.sensors, t.bindings, t.readings, t.journal, m_clock);
        QVERIFY_EXCEPTION_THROWN(importer.importFrom(bogus), StorageError);
    }
};

QTEST_GUILESS_MAIN(TestLegacyImport)
#include "test_legacyimport.moc"
