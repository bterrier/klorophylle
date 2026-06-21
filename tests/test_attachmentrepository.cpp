// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QtSql/QSqlQuery>

#include "clock.h"
#include "database.h"
#include "inmemoryattachmentrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "migrationrunner.h"
#include "schema.h"
#include "sqliteattachmentrepository.h"
#include "sqlitejournalrepository.h"
#include "sqliteplantrepository.h"

using namespace klr;

// Same dual-impl approach as test_journalrepository: every invariant runs against both the
// in-memory fakes and the SQLite repos. An attachment belongs to a journal entry (the SQLite layer
// enforces it with a foreign key), so the bodies create the parent plant + entry first.
namespace {

Plant makePlant()
{
    Plant p;
    p.id = PlantId::generate();
    p.displayName = QStringLiteral("subject");
    p.trackedSince = QDateTime::currentDateTimeUtc();
    return p;
}

JournalEntry makeEntry(PlantId plant, const QDateTime &ts)
{
    JournalEntry e;
    e.id = JournalEntryId::generate();
    e.plant = plant;
    e.timestamp = ts;
    e.kind = JournalEntryKind::Observation;
    e.note = QStringLiteral("a sighting");
    return e;
}

Attachment makeAttachment(JournalEntryId entry, const QDateTime &added, const QString &ref,
                          const QString &caption)
{
    Attachment a;
    a.id = AttachmentId::generate();
    a.entry = entry;
    a.fileRef = ref;
    a.caption = caption;
    a.addedAt = added;
    return a;
}

void checkForEntryAddedOrder(IPlantRepository &plants, IJournalRepository &journal,
                             IAttachmentRepository &attachments)
{
    const Plant p = makePlant();
    plants.add(p);
    const QDateTime t0(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
    const JournalEntry e = makeEntry(p.id, t0);
    const JournalEntry other = makeEntry(p.id, t0.addSecs(10));
    journal.add(e);
    journal.add(other);

    const Attachment before = makeAttachment(e.id, t0, QStringLiteral("attachments/a.jpg"),
                                             QStringLiteral("Before"));
    const Attachment after = makeAttachment(e.id, t0.addSecs(3600), QStringLiteral("attachments/b.jpg"),
                                            QStringLiteral("After"));
    const Attachment elsewhere = makeAttachment(other.id, t0, QStringLiteral("attachments/c.jpg"),
                                                QString());
    attachments.add(before);
    attachments.add(after);
    attachments.add(elsewhere);

    const QList<Attachment> ours = attachments.forEntry(e.id);
    QCOMPARE(ours.size(), 2);              // scoped to the entry
    QCOMPARE(ours.first().id, before.id);  // oldest-first (added order)
    QCOMPARE(ours.last().id, after.id);
    QCOMPARE(ours.first().caption, QStringLiteral("Before"));
    QCOMPARE(ours.first().fileRef, QStringLiteral("attachments/a.jpg"));

    QCOMPARE(attachments.all().size(), 3); // every row, across entries
}

void checkUpdateCaption(IPlantRepository &plants, IJournalRepository &journal,
                        IAttachmentRepository &attachments)
{
    const Plant p = makePlant();
    plants.add(p);
    const JournalEntry e = makeEntry(p.id, QDateTime::currentDateTimeUtc());
    journal.add(e);
    Attachment a = makeAttachment(e.id, QDateTime::currentDateTimeUtc(),
                                  QStringLiteral("attachments/a.jpg"), QStringLiteral("typo"));
    attachments.add(a);

    attachments.updateCaption(a.id, QStringLiteral("Before repot"));
    const QList<Attachment> ours = attachments.forEntry(e.id);
    QCOMPARE(ours.size(), 1);
    QCOMPARE(ours.first().caption, QStringLiteral("Before repot"));
    QCOMPARE(ours.first().fileRef, QStringLiteral("attachments/a.jpg")); // ref untouched
}

void checkRemove(IPlantRepository &plants, IJournalRepository &journal,
                IAttachmentRepository &attachments)
{
    const Plant p = makePlant();
    plants.add(p);
    const JournalEntry e = makeEntry(p.id, QDateTime::currentDateTimeUtc());
    journal.add(e);
    const Attachment a = makeAttachment(e.id, QDateTime::currentDateTimeUtc(),
                                        QStringLiteral("attachments/a.jpg"), QString());
    attachments.add(a);
    QCOMPARE(attachments.forEntry(e.id).size(), 1);
    attachments.remove(a.id);
    QVERIFY(attachments.forEntry(e.id).isEmpty());
    QVERIFY(attachments.all().isEmpty());
}

} // namespace

class TestAttachmentRepository : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    Database freshDb()
    {
        Database db = Database::openInMemory(m_clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        return db;
    }

private slots:
    void forEntryAddedOrder()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            InMemoryAttachmentRepository a;
            checkForEntryAddedOrder(p, j, a);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            SqliteAttachmentRepository a(db);
            checkForEntryAddedOrder(p, j, a);
        }
    }
    void updateCaption()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            InMemoryAttachmentRepository a;
            checkUpdateCaption(p, j, a);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            SqliteAttachmentRepository a(db);
            checkUpdateCaption(p, j, a);
        }
    }
    void remove()
    {
        {
            InMemoryPlantRepository p;
            InMemoryJournalRepository j;
            InMemoryAttachmentRepository a;
            checkRemove(p, j, a);
        }
        {
            Database db = freshDb();
            SqlitePlantRepository p(db);
            SqliteJournalRepository j(db);
            SqliteAttachmentRepository a(db);
            checkRemove(p, j, a);
        }
    }

    // SQLite-specific: deleting a journal entry cascade-deletes its attachment rows
    // (FK ON DELETE CASCADE, foreign_keys=ON per connection).
    void sqliteCascadeOnEntryDelete()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteJournalRepository journal(db);
        SqliteAttachmentRepository attachments(db);

        const Plant p = makePlant();
        plants.add(p);
        const JournalEntry e = makeEntry(p.id, QDateTime::currentDateTimeUtc());
        journal.add(e);
        attachments.add(makeAttachment(e.id, QDateTime::currentDateTimeUtc(),
                                       QStringLiteral("attachments/a.jpg"), QString()));
        attachments.add(makeAttachment(e.id, QDateTime::currentDateTimeUtc().addSecs(1),
                                       QStringLiteral("attachments/b.jpg"), QString()));
        QCOMPARE(attachments.forEntry(e.id).size(), 2);

        journal.remove(e.id);
        QVERIFY(attachments.forEntry(e.id).isEmpty()); // cascaded
        QVERIFY(attachments.all().isEmpty());
    }

    // SQLite-specific: each mutation writes exactly one change_log row (sync substrate, ADR 0024).
    void sqliteWritesChangeLog()
    {
        Database db = freshDb();
        SqlitePlantRepository plants(db);
        SqliteJournalRepository journal(db);
        SqliteAttachmentRepository attachments(db);

        const Plant p = makePlant();
        plants.add(p);
        const JournalEntry e = makeEntry(p.id, QDateTime::currentDateTimeUtc());
        journal.add(e);
        const Attachment a = makeAttachment(e.id, QDateTime::currentDateTimeUtc(),
                                            QStringLiteral("attachments/a.jpg"), QString());
        attachments.add(a);
        attachments.updateCaption(a.id, QStringLiteral("Before"));
        attachments.remove(a.id);

        QSqlQuery q(db.handle());
        QVERIFY(q.exec(QStringLiteral(
            "SELECT op FROM change_log WHERE entity='attachment' ORDER BY seq")));
        QStringList ops;
        while (q.next())
            ops << q.value(0).toString();
        QCOMPARE(ops, (QStringList{ QStringLiteral("insert"), QStringLiteral("update"),
                                    QStringLiteral("delete") }));
    }
};

QTEST_GUILESS_MAIN(TestAttachmentRepository)
#include "test_attachmentrepository.moc"
