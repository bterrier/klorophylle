// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "database.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqlitejournalrepository.h"
#include "sqliteplantrepository.h"

using namespace klr;

// Same dual-impl approach as test_plantrepository: every invariant runs against
// both the in-memory fakes and the SQLite repos. A journal entry belongs to a
// plant (the SQLite layer enforces this with a foreign key), so the bodies create
// the parent plant first via the plant repository.
namespace {

Plant makePlant()
{
    Plant p;
    p.id = PlantId::generate();
    p.displayName = QStringLiteral("subject");
    p.trackedSince = QDateTime::currentDateTimeUtc();
    return p;
}

JournalEntry makeEntry(PlantId plant, const QDateTime &ts, JournalEntryKind kind,
                       const QString &note)
{
    JournalEntry e;
    e.id = JournalEntryId::generate();
    e.plant = plant;
    e.timestamp = ts;
    e.kind = kind;
    e.note = note;
    return e;
}

void checkForPlantNewestFirst(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    const Plant other = makePlant();
    plants.add(subject);
    plants.add(other);
    const QDateTime t0 = QDateTime::currentDateTimeUtc();

    const JournalEntry first = makeEntry(subject.id, t0, JournalEntryKind::Watering,
                                         QStringLiteral("watered"));
    const JournalEntry second = makeEntry(subject.id, t0.addSecs(3600), JournalEntryKind::Note,
                                          QStringLiteral("new leaf"));
    const JournalEntry elsewhere = makeEntry(other.id, t0, JournalEntryKind::Note,
                                             QStringLiteral("other plant"));
    journal.add(first);
    journal.add(second);
    journal.add(elsewhere);

    const QList<JournalEntry> entries = journal.forPlant(subject.id);
    QCOMPARE(entries.size(), 2);             // scoped to the plant
    QCOMPARE(entries.first().id, second.id); // newest-first
    QCOMPARE(entries.last().id, first.id);
}

void checkUpdate(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    plants.add(subject);
    JournalEntry e = makeEntry(subject.id, QDateTime::currentDateTimeUtc(),
                               JournalEntryKind::Note, QStringLiteral("typo"));
    journal.add(e);
    e.note = QStringLiteral("fixed");
    e.kind = JournalEntryKind::Observation;
    journal.update(e);

    const QList<JournalEntry> entries = journal.forPlant(subject.id);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().note, QStringLiteral("fixed"));
    QCOMPARE(entries.first().kind, JournalEntryKind::Observation);
}

void checkEditedAtRoundTrip(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    plants.add(subject);
    const QDateTime t0(QDate(2026, 1, 2), QTime(3, 4, 5, 678), QTimeZone::UTC);

    // A freshly-added entry is never-edited: editedAt round-trips as nullopt.
    JournalEntry fresh = makeEntry(subject.id, t0, JournalEntryKind::Note,
                                   QStringLiteral("fresh"));
    journal.add(fresh);

    // An entry that already carries an editedAt round-trips that value verbatim.
    const QDateTime edited(QDate(2026, 2, 3), QTime(6, 7, 8, 9), QTimeZone::UTC);
    JournalEntry stamped = makeEntry(subject.id, t0.addSecs(3600), JournalEntryKind::Note,
                                     QStringLiteral("stamped"));
    stamped.editedAt = edited;
    journal.add(stamped);

    const QList<JournalEntry> entries = journal.forPlant(subject.id);
    QCOMPARE(entries.size(), 2);
    // Newest-first: stamped (t0+1h) then fresh (t0).
    QCOMPARE(entries.first().id, stamped.id);
    QVERIFY(entries.first().editedAt.has_value());
    QCOMPARE(*entries.first().editedAt, edited);
    QCOMPARE(entries.last().id, fresh.id);
    QVERIFY(!entries.last().editedAt.has_value()); // never edited
}

void checkUpdateSetsEditedAtPreservesTimestamp(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    plants.add(subject);
    const QDateTime entryDate(QDate(2026, 1, 2), QTime(3, 4, 5, 678), QTimeZone::UTC);

    JournalEntry e = makeEntry(subject.id, entryDate, JournalEntryKind::Note,
                               QStringLiteral("typo"));
    journal.add(e); // editedAt nullopt

    // An edit sets editedAt and changes the note/kind, but keeps the original entry date.
    const QDateTime editDate(QDate(2026, 5, 6), QTime(7, 8, 9, 10), QTimeZone::UTC);
    e.note = QStringLiteral("fixed");
    e.kind = JournalEntryKind::Observation;
    e.editedAt = editDate;
    journal.update(e);

    const QList<JournalEntry> entries = journal.forPlant(subject.id);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().timestamp, entryDate); // entry date survives the edit
    QVERIFY(entries.first().editedAt.has_value());
    QCOMPARE(*entries.first().editedAt, editDate);  // edit date persisted
    QCOMPARE(entries.first().note, QStringLiteral("fixed"));
    QCOMPARE(entries.first().kind, JournalEntryKind::Observation);
}

JournalEntry makeGlobalEntry(const QDateTime &ts, JournalEntryKind kind, const QString &note)
{
    JournalEntry e;
    e.id = JournalEntryId::generate();
    e.plant = std::nullopt;                  // a global (plant-less) entry (ADR 0022)
    e.timestamp = ts;
    e.kind = kind;
    e.note = note;
    return e;
}

void checkGlobalEntries(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    plants.add(subject);
    const QDateTime t0(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);

    const JournalEntry plantEntry = makeEntry(subject.id, t0, JournalEntryKind::Note,
                                              QStringLiteral("on a plant"));
    const JournalEntry g1 = makeGlobalEntry(t0, JournalEntryKind::Note, QStringLiteral("first"));
    const JournalEntry g2 = makeGlobalEntry(t0.addSecs(3600), JournalEntryKind::Memory,
                                            QStringLiteral("global memory"));
    journal.add(plantEntry);
    journal.add(g1);
    journal.add(g2);

    // globalEntries() returns ONLY the plant-less entries, newest-first.
    const QList<JournalEntry> globals = journal.globalEntries();
    QCOMPARE(globals.size(), 2);
    QCOMPARE(globals.first().id, g2.id); // newest-first
    QCOMPARE(globals.last().id, g1.id);
    QVERIFY(!globals.first().plant.has_value());

    // The plant-scoped walk excludes globals; the global walk excludes plant entries.
    const QList<JournalEntry> ofPlant = journal.forPlant(subject.id);
    QCOMPARE(ofPlant.size(), 1);
    QCOMPARE(ofPlant.first().id, plantEntry.id);
}

void checkRemove(IPlantRepository &plants, IJournalRepository &journal)
{
    const Plant subject = makePlant();
    plants.add(subject);
    const JournalEntry e = makeEntry(subject.id, QDateTime::currentDateTimeUtc(),
                                     JournalEntryKind::Pruning, QString());
    journal.add(e);
    QCOMPARE(journal.forPlant(subject.id).size(), 1);
    journal.remove(e.id);
    QVERIFY(journal.forPlant(subject.id).isEmpty());
}

} // namespace

class TestJournalRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void forPlantNewestFirst()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkForPlantNewestFirst(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkForPlantNewestFirst(p, j);
        }
    }
    void update()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkUpdate(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkUpdate(p, j);
        }
    }
    void remove()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkRemove(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkRemove(p, j);
        }
    }
    void editedAtRoundTrip()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkEditedAtRoundTrip(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkEditedAtRoundTrip(p, j);
        }
    }
    void updateSetsEditedAtPreservesTimestamp()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkUpdateSetsEditedAtPreservesTimestamp(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkUpdateSetsEditedAtPreservesTimestamp(p, j);
        }
    }

    void globalEntries()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            checkGlobalEntries(p, j);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            checkGlobalEntries(p, j);
        }
    }

    // SQLite-specific: deleting a plant cascade-deletes its journal entries
    // (FK ON DELETE CASCADE, with foreign_keys=ON applied per connection).
    void sqliteCascadeOnPlantDelete()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteJournalRepository journal(db);

        const Plant p = makePlant();
        plants.add(p);
        journal.add(makeEntry(p.id, QDateTime::currentDateTimeUtc(),
                              JournalEntryKind::Watering, QStringLiteral("a")));
        journal.add(makeEntry(p.id, QDateTime::currentDateTimeUtc().addSecs(1),
                              JournalEntryKind::Note, QStringLiteral("b")));
        QCOMPARE(journal.forPlant(p.id).size(), 2);

        plants.remove(p.id);
        QVERIFY(journal.forPlant(p.id).isEmpty()); // cascaded
    }

    // SQLite-specific: a NULL plant_id (global entry) is NOT cascaded when a plant is deleted —
    // global memory/notes survive (ADR 0022). Also proves NULL plant_id inserts after the v7 rebuild.
    void sqliteGlobalSurvivesPlantDelete()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteJournalRepository journal(db);

        const Plant p = makePlant();
        plants.add(p);
        journal.add(makeEntry(p.id, QDateTime::currentDateTimeUtc(),
                              JournalEntryKind::Note, QStringLiteral("plant note")));
        const JournalEntry g = makeGlobalEntry(QDateTime::currentDateTimeUtc(),
                                               JournalEntryKind::Memory, QStringLiteral("global"));
        journal.add(g);
        QCOMPARE(journal.globalEntries().size(), 1);

        plants.remove(p.id);
        QVERIFY(journal.forPlant(p.id).isEmpty());      // plant entry cascaded away
        QCOMPARE(journal.globalEntries().size(), 1);    // global entry survived
        QCOMPARE(journal.globalEntries().first().id, g.id);
    }
};

QTEST_GUILESS_MAIN(TestJournalRepository)
#include "test_journalrepository.moc"
