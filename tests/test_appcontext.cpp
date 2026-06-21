// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "appcontext.h"
#include "backupimporter.h"
#include "backupserializer.h"
#include "carestatus.h"
#include "catalogentry.h"
#include "catalogsearchmodel.h"
#include "clock.h"
#include "csvcatalogrepository.h"
#include "attachment.h"
#include "ids.h"
#include "inmemoryattachmentfilestore.h"
#include "inmemoryattachmentrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemorykeyvaluestore.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "inmemorysyncstaterepository.h"
#include "journalentry.h"
#include "journalformat.h"
#include "legacyimporter.h"
#include "plant.h"
#include "plantjournalmodel.h"
#include "plantlistmodel.h"
#include "reading.h"
#include "readingscsvexporter.h"
#include "registeredsensorsmodel.h"
#include "sensordeleter.h"
#include "seriesmodel.h"
#include "settingsstore.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimeZone>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

using namespace klr;

namespace {
CatalogEntry catEntry(const QString &key, const QString &common)
{
    CatalogEntry e;
    e.key = key;
    e.commonName = common;
    return e;
}
} // namespace

// Exercises the plant-first UI logic (AppContext's invokables + the list models)
// with no BLE and no QML — the same flow the screens drive. In-memory repos keep
// it fast and deterministic.
class TestAppContext : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    static QString firstPlantId(PlantListModel &m)
    {
        return m.data(m.index(0, 0), PlantListModel::PlantIdRole).toString();
    }

    // A one-device legacy data.db, just enough to drive the import wiring (the full
    // mapping is covered by test_legacyimport).
    static void writeLegacyFixture(const QString &path)
    {
        const QString conn = QStringLiteral("appcontext-legacy-fixture");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral(
                "CREATE TABLE devices (deviceAddr TEXT PRIMARY KEY, deviceAddrMAC TEXT, "
                "deviceName TEXT, deviceModel TEXT, associatedName TEXT, lastSeen DATETIME)")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO devices(deviceAddr, deviceAddrMAC, deviceName, deviceModel, associatedName) "
                "VALUES('AA:BB:CC', 'AA:BB:CC', 'Flower care', 'Flower care', 'Orchid')")));
        }
        QSqlDatabase::removeDatabase(conn);
    }

private slots:
    void addSelectJournalFlow()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        QCOMPARE(plants.rowCount(), 0);

        // Add a sensorless plant — it appears in the list and persists in the repo.
        ctx.addPlant(QStringLiteral("Living-room ficus"), QStringLiteral("Ficus elastica"));
        QCOMPARE(plants.rowCount(), 1);
        QCOMPARE(plantRepo.all().size(), 1);

        // Selecting it exposes its name/species and binds the journal model.
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        QCOMPARE(ctx.selectedPlantName(), QStringLiteral("Living-room ficus"));
        QCOMPARE(ctx.selectedPlantSpecies(), QStringLiteral("Ficus elastica"));
        QCOMPARE(journal.rowCount(), 0);

        // Add journal entries — they show newest-first in the bound model.
        ctx.addJournalEntry(int(JournalEntryKind::Watering), QStringLiteral("gave water"));
        ctx.addJournalEntry(int(JournalEntryKind::Note), QStringLiteral("new leaf"));
        QCOMPARE(journal.rowCount(), 2);

        const QString entryId =
            journal.data(journal.index(0, 0), PlantJournalModel::EntryIdRole).toString();
        ctx.removeJournalEntry(entryId);
        QCOMPARE(journal.rowCount(), 1);
    }

    void editJournalEntryKeepsEntryDate()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        ctx.addPlant(QStringLiteral("Fern"), QString());
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        const PlantId plant{ QUuid::fromString(pid) };

        // Two entries so we can also prove an edit never reorders the timeline.
        ctx.addJournalEntry(int(JournalEntryKind::Watering), QStringLiteral("older"));
        ctx.addJournalEntry(int(JournalEntryKind::Note), QStringLiteral("newer"));
        QCOMPARE(journal.rowCount(), 2);

        const auto idsOf = [](const QList<JournalEntry> &es) {
            QStringList ids;
            for (const JournalEntry &e : es)
                ids << e.id.toString();
            return ids;
        };
        const QList<JournalEntry> before = journalRepo.forPlant(plant);
        const QStringList orderBefore = idsOf(before);
        // The oldest entry is last (newest-first ordering).
        const JournalEntry oldest = before.last();
        QVERIFY(!oldest.editedAt.has_value()); // freshly added => never edited
        const QDateTime entryDate = oldest.timestamp;

        // Edit the oldest entry's note + kind.
        ctx.editJournalEntry(oldest.id.toString(), int(JournalEntryKind::Observation),
                             QStringLiteral("  amended  "));

        const QList<JournalEntry> after = journalRepo.forPlant(plant);
        QCOMPARE(after.size(), 2);                       // edit, not insert
        QCOMPARE(idsOf(after), orderBefore);             // never reorders
        const JournalEntry &edited = after.last();
        QCOMPARE(edited.id, oldest.id);
        QCOMPARE(edited.timestamp, entryDate);           // entry date preserved
        QCOMPARE(edited.note, QStringLiteral("amended")); // trimmed + applied
        QCOMPARE(edited.kind, JournalEntryKind::Observation);
        QVERIFY(edited.editedAt.has_value());            // edit date stamped
        QVERIFY(*edited.editedAt >= entryDate);
    }

    void journalPhotoAddCaptionRemove()
    {
        // The photo edge (ADR 0024): addPhotoToEntry copies the file in + writes a clock-stamped
        // row; setPhotoCaption updates it; removePhoto deletes the row AND the file. Exercised over
        // the in-memory repo + RAM file store, with the journal model resolving the AttachmentsRole.
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemoryAttachmentRepository attachmentRepo;
        InMemoryAttachmentFileStore fileStore;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo, &attachmentRepo, &fileStore);
        m_clock.t = 1700000000000LL;
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &m_clock, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       &attachmentRepo, &fileStore);

        ctx.addPlant(QStringLiteral("Ficus"), QString());
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        ctx.addJournalEntry(int(JournalEntryKind::Observation), QStringLiteral("a leaf"));
        const QString entryId =
            journal.data(journal.index(0, 0), PlantJournalModel::EntryIdRole).toString();

        // A real source file the store copies in.
        QTemporaryDir src;
        QVERIFY(src.isValid());
        const QString srcPath = QDir(src.path()).filePath(QStringLiteral("leaf.png"));
        {
            QFile f(srcPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("PNGDATA"));
        }

        ctx.addPhotoToEntry(entryId, QUrl::fromLocalFile(srcPath).toString(), QStringLiteral("Front"));

        // One row, on this entry, stamped from the injected clock, with bytes in the store.
        const QList<Attachment> all = attachmentRepo.all();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().entry.toString(), entryId);
        QCOMPARE(all.first().caption, QStringLiteral("Front"));
        QCOMPARE(all.first().addedAt,
                 QDateTime::fromMSecsSinceEpoch(m_clock.t, QTimeZone::UTC));
        const std::optional<QByteArray> bytes = fileStore.read(all.first().fileRef);
        QVERIFY(bytes.has_value());
        QCOMPARE(*bytes, QByteArray("PNGDATA"));

        // The journal model surfaces it through AttachmentsRole (one {attachmentId,url,caption}).
        const QVariantList shown =
            journal.data(journal.index(0, 0), PlantJournalModel::AttachmentsRole).toList();
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.first().toMap().value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Front"));

        // Caption edit.
        const QString aid = all.first().id.toString();
        ctx.setPhotoCaption(aid, QStringLiteral("Back"));
        QCOMPARE(attachmentRepo.all().first().caption, QStringLiteral("Back"));

        // Remove deletes the row AND the backing file.
        const QString fileRef = attachmentRepo.all().first().fileRef;
        ctx.removePhoto(aid);
        QVERIFY(attachmentRepo.all().isEmpty());
        QVERIFY(!fileStore.read(fileRef).has_value());
    }

    void saveJournalEntryStagesAndReconcilesPhotos()
    {
        // The add/edit dialog's one-shot Save: saveJournalEntry creates-or-updates the
        // entry then reconciles photos — attaching staged file URLs (possible because the entry now
        // exists) and removing the ids the user tapped. Proven over the in-memory repo + RAM store.
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemoryAttachmentRepository attachmentRepo;
        InMemoryAttachmentFileStore fileStore;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo, &attachmentRepo, &fileStore);
        m_clock.t = 1700000000000LL;
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &m_clock, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       &attachmentRepo, &fileStore);

        ctx.addPlant(QStringLiteral("Ficus"), QString());
        ctx.selectPlant(firstPlantId(plants));

        // Two real source files to stage.
        QTemporaryDir src;
        QVERIFY(src.isValid());
        auto makeFile = [&](const QString &name, const QByteArray &data) {
            const QString p = QDir(src.path()).filePath(name);
            QFile f(p);
            [[maybe_unused]] const bool ok = f.open(QIODevice::WriteOnly);
            f.write(data);
            return QUrl::fromLocalFile(p).toString();
        };
        const QString urlA = makeFile(QStringLiteral("a.png"), QByteArray("AAA"));
        const QString urlB = makeFile(QStringLiteral("b.png"), QByteArray("BBB"));

        // Add mode: empty id → creates the entry and attaches both staged photos.
        const QString id = ctx.saveJournalEntry(QString(), int(JournalEntryKind::Note),
                                                QStringLiteral("with photos"),
                                                { urlA, urlB }, {});
        QVERIFY(!id.isEmpty());
        QCOMPARE(journalRepo.forPlant(PlantId{ QUuid::fromString(firstPlantId(plants)) }).size(), 1);
        QCOMPARE(attachmentRepo.forEntry(JournalEntryId{ QUuid::fromString(id) }).size(), 2);

        // Edit mode: drop one existing photo and stage a new one in the same Save.
        const QString dropId =
            attachmentRepo.forEntry(JournalEntryId{ QUuid::fromString(id) }).first().id.toString();
        const QString urlC = makeFile(QStringLiteral("c.png"), QByteArray("CCC"));
        ctx.saveJournalEntry(id, int(JournalEntryKind::Note), QStringLiteral("edited"),
                             { urlC }, { dropId });
        const QList<Attachment> after = attachmentRepo.forEntry(JournalEntryId{ QUuid::fromString(id) });
        QCOMPARE(after.size(), 2); // started 2, removed 1, added 1
        for (const Attachment &a : after)
            QVERIFY(a.id.toString() != dropId);
        // The note edit landed too.
        QCOMPARE(journalRepo.forPlant(PlantId{ QUuid::fromString(firstPlantId(plants)) })
                     .first().note,
                 QStringLiteral("edited"));
    }

    void removePlantClearsSelection()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        ctx.addPlant(QStringLiteral("Doomed"), QString());
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        ctx.addJournalEntry(int(JournalEntryKind::Pruning), QString());
        QCOMPARE(journal.rowCount(), 1);

        ctx.removePlant(pid);
        QCOMPARE(plants.rowCount(), 0);
        QVERIFY(ctx.selectedPlantName().isEmpty());
        QCOMPARE(journal.rowCount(), 0); // journal unbound on delete of the selected plant
    }

    void journalKindsCoverEveryKind()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        // One rendering label per enum value (Note..Memory), all non-empty. journalKinds() is the
        // full render list; the creatable subset (which omits Memory) is checked separately.
        QCOMPARE(ctx.journalKinds().size(), int(JournalEntryKind::Memory) + 1);
        for (const QString &label : ctx.journalKinds())
            QVERIFY(!label.isEmpty());
    }

    void creatableJournalKindsOmitsMemory()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        // The user can create Note..Observation, but NOT Memory (agent-authored).
        const QStringList creatable = ctx.creatableJournalKinds();
        QCOMPARE(creatable.size(), int(JournalEntryKind::Observation) + 1);
        const QString memoryLabel = journalKindLabel(JournalEntryKind::Memory);
        QVERIFY(!creatable.contains(memoryLabel));
        QVERIFY(ctx.journalKinds().contains(memoryLabel)); // still rendered in the full list
    }

    void editingMemoryEntryBumpsOnlyEditedAt()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        ctx.addPlant(QStringLiteral("Fern"), QString());
        const QString pid = firstPlantId(plants);
        const PlantId plant{ QUuid::fromString(pid) };

        // A user cannot create Memory through addJournalEntry; the agent authors it. Seed one
        // directly, then prove a USER edit is accepted and moves only the edit date.
        const QDateTime entryDate = QDateTime::currentDateTimeUtc().addDays(-2);
        const JournalEntry seed{ JournalEntryId::generate(), plant, entryDate,
                                 JournalEntryKind::Memory, QStringLiteral("waters lightly") };
        journalRepo.add(seed);
        ctx.selectPlant(pid);

        ctx.editJournalEntry(seed.id.toString(), int(JournalEntryKind::Memory),
                             QStringLiteral("waters lightly; sensitive to repotting"));

        const QList<JournalEntry> after = journalRepo.forPlant(plant);
        QCOMPARE(after.size(), 1);
        const JournalEntry &edited = after.first();
        QCOMPARE(edited.id, seed.id);
        QCOMPARE(edited.kind, JournalEntryKind::Memory);             // kind preserved
        QCOMPARE(edited.timestamp, entryDate);                       // entry date untouched (no reorder)
        QCOMPARE(edited.note, QStringLiteral("waters lightly; sensitive to repotting"));
        QVERIFY(edited.editedAt.has_value());                       // edit date stamped
        QVERIFY(*edited.editedAt >= entryDate);
    }

    void editJournalEntryCannotConvertIntoMemory()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        ctx.addPlant(QStringLiteral("Fern"), QString());
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        ctx.addJournalEntry(int(JournalEntryKind::Note), QStringLiteral("a note"));
        const JournalEntry note = journalRepo.forPlant(PlantId{ QUuid::fromString(pid) }).first();

        // Trying to turn a normal entry INTO Memory is rejected (only the agent authors Memory).
        ctx.editJournalEntry(note.id.toString(), int(JournalEntryKind::Memory),
                             QStringLiteral("sneaky"));
        const JournalEntry after = journalRepo.forPlant(PlantId{ QUuid::fromString(pid) }).first();
        QCOMPARE(after.kind, JournalEntryKind::Note);   // unchanged
        QCOMPARE(after.note, QStringLiteral("a note")); // unchanged
        QVERIFY(!after.editedAt.has_value());           // no edit happened
    }

    // --- global journal ---

    void globalCreatableJournalKindsIsNoteOnly()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        // The global journal is plant-less: only Note is user-creatable (care kinds + Observation
        // are plant-scoped; Memory is agent-authored). ADR 0022.
        const QStringList creatable = ctx.globalCreatableJournalKinds();
        QCOMPARE(creatable.size(), 1);
        QCOMPARE(creatable.first(), journalKindLabel(JournalEntryKind::Note));
        QVERIFY(!creatable.contains(journalKindLabel(JournalEntryKind::Memory)));
        QVERIFY(!creatable.contains(journalKindLabel(JournalEntryKind::Watering)));
    }

    void addGlobalJournalEntryWritesPlantLessNote()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        ctx.addGlobalJournalEntry(int(JournalEntryKind::Note), QStringLiteral("buy more fertilizer"));

        const QList<JournalEntry> globals = journalRepo.globalEntries();
        QCOMPARE(globals.size(), 1);
        QVERIFY(!globals.first().plant.has_value()); // plant-less
        QCOMPARE(globals.first().kind, JournalEntryKind::Note);
        QCOMPARE(globals.first().note, QStringLiteral("buy more fertilizer"));
        // The bound global-journal model reflects it.
        QCOMPARE(ctx.globalJournal()->rowCount(QModelIndex()), 1);

        // Non-Note kinds are rejected (care kinds are plant-scoped; Memory is agent-authored).
        ctx.addGlobalJournalEntry(int(JournalEntryKind::Watering), QStringLiteral("nope"));
        ctx.addGlobalJournalEntry(int(JournalEntryKind::Memory), QStringLiteral("nope"));
        QCOMPARE(journalRepo.globalEntries().size(), 1); // still just the Note
    }

    void editingGlobalMemoryEntryBumpsOnlyEditedAt()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        // Seed the agent's global Memory entry directly (the user can't create Memory), then prove a
        // USER edit is accepted and moves only the edit date — the global mirror of the plant-memory invariant.
        const QDateTime entryDate = QDateTime::currentDateTimeUtc().addDays(-2);
        const JournalEntry seed{ JournalEntryId::generate(), std::nullopt, entryDate,
                                 JournalEntryKind::Memory, QStringLiteral("hard tap water") };
        journalRepo.add(seed);

        ctx.editGlobalJournalEntry(seed.id.toString(), int(JournalEntryKind::Memory),
                                   QStringLiteral("hard tap water; owner travels often"));

        const QList<JournalEntry> after = journalRepo.globalEntries();
        QCOMPARE(after.size(), 1);
        const JournalEntry &edited = after.first();
        QCOMPARE(edited.id, seed.id);
        QVERIFY(!edited.plant.has_value());                          // still global
        QCOMPARE(edited.kind, JournalEntryKind::Memory);             // kind preserved
        QCOMPARE(edited.timestamp, entryDate);                       // entry date untouched (no reorder)
        QCOMPARE(edited.note, QStringLiteral("hard tap water; owner travels often"));
        QVERIFY(edited.editedAt.has_value());                       // edit date stamped
    }

    void catalogSearchAndSpeciesAssociation()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        CsvCatalogRepository catalog({
            catEntry(QStringLiteral("Ficus elastica"), QStringLiteral("Rubber plant")),
            catEntry(QStringLiteral("Aloe vera"), QStringLiteral("Aloe")),
        });
        CatalogSearchModel catalogModel(catalog);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, &catalog, &catalogModel);

        // searchCatalog drives the results model the picker binds to.
        ctx.searchCatalog(QStringLiteral("ficus"));
        QCOMPARE(catalogModel.rowCount(), 1);
        QCOMPARE(catalogModel.data(catalogModel.index(0, 0), CatalogSearchModel::KeyRole).toString(),
                 QStringLiteral("Ficus elastica"));

        // A plant starts speciesless; associating a species persists the catalog key
        // and the display string enriches with the common name.
        ctx.addPlant(QStringLiteral("My ficus"), QString());
        const QString pid = firstPlantId(plants);
        ctx.selectPlant(pid);
        QVERIFY(ctx.selectedPlantSpecies().isEmpty());
        QVERIFY(ctx.selectedPlantSpeciesDisplay().isEmpty());

        ctx.setSelectedPlantSpecies(QStringLiteral("Ficus elastica"));
        QCOMPARE(ctx.selectedPlantSpecies(), QStringLiteral("Ficus elastica"));
        QCOMPARE(ctx.selectedPlantSpeciesDisplay(),
                 QStringLiteral("Ficus elastica · Rubber plant"));
        const std::optional<Plant> stored = plantRepo.get(PlantId{ QUuid::fromString(pid) });
        QVERIFY(stored.has_value());
        QCOMPARE(stored->species, QStringLiteral("Ficus elastica"));

        // Clearing the species is supported (sensorless/speciesless is valid, goal #1).
        ctx.setSelectedPlantSpecies(QString());
        QVERIFY(ctx.selectedPlantSpecies().isEmpty());
        QVERIFY(ctx.selectedPlantSpeciesDisplay().isEmpty());
    }

    void importLegacyDatabasePopulatesPlantsAndReports()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        writeLegacyFixture(legacy);

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(m_clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        LegacyImporter importer(plantRepo, sensorRepo, bindingRepo, readingRepo, journalRepo,
                                m_clock);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &importer);

        QSignalSpy spy(&ctx, &AppContext::importFinished);
        QCOMPARE(plants.rowCount(), 0);

        // The QML picker hands back a file:// URL; the device becomes a plant and the list
        // model is refreshed so it shows immediately.
        ctx.importLegacyDatabase(QUrl::fromLocalFile(legacy).toString());
        QCOMPARE(plants.rowCount(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toBool(), true); // ok

        // A non-WatchFlower / missing file reports failure instead of throwing into QML.
        ctx.importLegacyDatabase(QUrl::fromLocalFile(dir.filePath(QStringLiteral("nope.db"))).toString());
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(1).toBool(), false);
    }

    void exportBackupWritesFileThenRestoreRefreshes()
    {
        // Redirect the home dir so the DocumentsLocation/Klorophylle export folder lands in
        // a temp area, not the real Documents.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("XDG_CONFIG_HOME");

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(m_clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        InMemoryCareThresholdRepository thresholdRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);

        plantRepo.add(Plant{ PlantId::generate(), QStringLiteral("Fern"), QString(),
                             QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC) });

        ReadingsCsvExporter csv(plantRepo, bindingRepo, readingRepo, sensorRepo);
        BackupSerializer serializer(plantRepo, sensorRepo, bindingRepo, readingRepo, journalRepo,
                                    thresholdRepo, m_clock);
        BackupImporter importer(plantRepo, sensorRepo, bindingRepo, readingRepo, journalRepo,
                                thresholdRepo, m_clock);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &thresholdRepo, nullptr, nullptr,
                       nullptr, &csv, &serializer, &importer, nullptr, &m_clock);

        QSignalSpy exportSpy(&ctx, &AppContext::exportFinished);
        const QString path = ctx.exportBackup();
        QVERIFY(!path.isEmpty());
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(exportSpy.count(), 1);
        QCOMPARE(exportSpy.first().at(1).toBool(), true);                 // ok
        QVERIFY(!exportSpy.first().at(2).toString().isEmpty());           // folder URL for "Show in folder"

        // Restoring the just-written backup is idempotent and refreshes the plant list.
        QSignalSpy restoreSpy(&ctx, &AppContext::restoreFinished);
        ctx.restoreBackup(QUrl::fromLocalFile(path).toString());
        QCOMPARE(restoreSpy.count(), 1);
        QCOMPARE(restoreSpy.first().at(1).toBool(), true);
        QCOMPARE(plants.rowCount(), 1); // no duplicate

        QFile::remove(path);
    }

    void csvExportWindowFollowsSelectedPeriod()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("XDG_CONFIG_HOME");

        // A fixed "now" so the relative windows ("last 7 days" etc.) are deterministic.
        const QDateTime now(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::UTC);
        FakeClock clock;
        clock.t = now.toMSecsSinceEpoch();

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);

        const Plant fern{ PlantId::generate(), QStringLiteral("Fern"), QString(),
                          QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC) };
        plantRepo.add(fern);
        const SensorId s = sensorRepo.ensure(HandleKind::Mac, QStringLiteral("AA"),
                                             QStringLiteral("FlowerCare"));
        bindingRepo.bind(fern.id, s, fern.trackedSince, std::nullopt);
        // One reading inside a 7-day window, one well outside it.
        const auto recent = Reading{ Quantity::SoilMoisture, 40.0, Unit::Percent,
                                     now.addDays(-2), Provenance::History };
        const auto old = Reading{ Quantity::SoilMoisture, 38.0, Unit::Percent,
                                  now.addDays(-40), Provenance::History };
        readingRepo.append(s, std::array{ old, recent });

        ReadingsCsvExporter csv(plantRepo, bindingRepo, readingRepo, sensorRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, &csv, nullptr, nullptr, &settings, &clock);

        auto rowCount = [](const QString &path) {
            QFile f(path);
            [[maybe_unused]] const bool opened = f.open(QIODevice::ReadOnly | QIODevice::Text);
            const QStringList lines = QString::fromUtf8(f.readAll())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            return lines.size() - 1; // drop the header
        };

        // "Last 7 days" (index 2) keeps only the recent reading.
        settings.setExportPeriodIndex(2);
        const QString windowed = ctx.exportReadingsCsv();
        QVERIFY(!windowed.isEmpty());
        QCOMPARE(rowCount(windowed), 1);
        QFile::remove(windowed);

        // "All data" (index 0) returns both readings.
        settings.setExportPeriodIndex(0);
        const QString all = ctx.exportReadingsCsv();
        QVERIFY(!all.isEmpty());
        QCOMPARE(rowCount(all), 2);
        QFile::remove(all);
    }

    void exportWithoutHelpersReportsUnavailable()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal);

        QSignalSpy exportSpy(&ctx, &AppContext::exportFinished);
        QVERIFY(ctx.exportBackup().isEmpty());
        QCOMPARE(exportSpy.count(), 1);
        QCOMPARE(exportSpy.first().at(1).toBool(), false);

        QSignalSpy restoreSpy(&ctx, &AppContext::restoreFinished);
        ctx.restoreBackup(QStringLiteral("file:///nope.json"));
        QCOMPARE(restoreSpy.count(), 1);
        QCOMPARE(restoreSpy.first().at(1).toBool(), false);
    }

    void importSeedsCareThresholdsForSpeciesedPlants()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString legacy = dir.filePath(QStringLiteral("data.db"));
        const QString conn = QStringLiteral("appcontext-seed-fixture");
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
                "VALUES('AA:BB:CC', 'AA:BB:CC', 'Flower care', 'Flower care', 'Desk ficus')")));
            QVERIFY(q.exec(QStringLiteral(
                "INSERT INTO plants(plantId, plantName, plantCache, plantStart, deviceAddr) "
                "VALUES(1, 'Ficus elastica', '', '2026-01-01T00:00:00.000', 'AA:BB:CC')")));
        }
        QSqlDatabase::removeDatabase(conn);

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(m_clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        InMemoryCareThresholdRepository thresholds;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);

        CatalogEntry ficus;
        ficus.key = QStringLiteral("Ficus elastica");
        ficus.soilMoistureMin = 15.0;
        ficus.soilMoistureMax = 60.0;
        CsvCatalogRepository catalog({ ficus });

        LegacyImporter importer(plantRepo, sensorRepo, bindingRepo, readingRepo, journalRepo,
                                m_clock);
        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, &catalog, nullptr, nullptr, &thresholds, nullptr, &importer);

        ctx.importLegacyDatabase(QUrl::fromLocalFile(legacy).toString());

        // The imported plant got its species. Data-driven model: NOTHING is materialised
        // into the threshold table — care judgment derives the ranges from species+catalog
        // live (covered by test_plantlisthealth), so no seeding happens at import.
        const QList<Plant> all = plantRepo.all();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().species, QStringLiteral("Ficus elastica"));
        QVERIFY(thresholds.thresholdsFor(all.first().id).isEmpty());
    }

    void selectingAndChangingSpeciesNeverSeedsOrClobbers()
    {
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemoryCareThresholdRepository thresholds;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);

        CatalogEntry ficus;
        ficus.key = QStringLiteral("Ficus elastica");
        ficus.soilMoistureMin = 15.0;
        ficus.soilMoistureMax = 60.0;
        CsvCatalogRepository catalog({ ficus });

        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, &catalog, nullptr, nullptr, &thresholds);

        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("Legacy ficus");
        p.species = QStringLiteral("Ficus elastica");
        plantRepo.add(p);

        // Opening a speciesed plant does NOT materialise thresholds (data-driven — the
        // ranges are derived from the catalog on demand, no UI-action seeding).
        ctx.selectPlant(p.id.toString());
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty());

        // A user override is preserved across re-selection (never clobbered by seeding).
        thresholds.setRange(p.id, CareRange{ Quantity::SoilMoisture, 99.0, std::nullopt });
        ctx.selectPlant(p.id.toString());
        const QList<CareRange> after = thresholds.thresholdsFor(p.id);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.first().min, std::optional<double>(99.0));

        // Changing the species DROPS the old overrides (so the new species' ideals apply).
        ctx.setSelectedPlantSpecies(QStringLiteral("Aloe vera"));
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty());
    }

    // loadSensorHistory fills the per-sensor history chart (plant-agnostic) from the
    // sensor-keyed reading store, independent of any plant/binding.
    void loadSensorHistoryFillsTheSeries()
    {
        const QDateTime now(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::UTC);
        FakeClock clock;
        clock.t = now.toMSecsSinceEpoch();

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        RegisteredSensorsModel registered(sensorRepo, bindingRepo, readingRepo, clock);
        SensorDeleter deleter(sensorRepo, bindingRepo, readingRepo);

        const SensorId s = sensorRepo.ensure(HandleKind::Mac, QStringLiteral("AA:BB"),
                                             QStringLiteral("FlowerCare"));
        const Reading r[] = {
            { Quantity::SoilMoisture, 40.0, Unit::Percent, now.addDays(-2), Provenance::History },
            { Quantity::SoilMoisture, 44.0, Unit::Percent, now.addDays(-1), Provenance::History },
        };
        readingRepo.append(s, std::span<const Reading>(r, std::size(r)));

        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &clock, nullptr, nullptr,
                       &sensorRepo, &readingRepo, nullptr, nullptr, &registered, &deleter,
                       &bindingRepo);

        ctx.loadSensorHistory(s.toString(), int(Quantity::SoilMoisture));
        auto *series = qobject_cast<SeriesModel *>(ctx.sensorHistory());
        QVERIFY(series);
        QCOMPARE(series->points().size(), 2);
    }

    // removeRegisteredSensor deletes an UNBOUND sensor (+ its data) and drops it from the
    // registered list, but REFUSES a sensor still bound to a plant.
    void removeRegisteredSensorDeletesUnboundRefusesBound()
    {
        FakeClock clock;
        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(clock);
        InMemoryBindingRepository bindingRepo;
        InMemoryReadingRepository readingRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);
        RegisteredSensorsModel registered(sensorRepo, bindingRepo, readingRepo, clock);
        SensorDeleter deleter(sensorRepo, bindingRepo, readingRepo);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Plant p{ PlantId::generate(), QStringLiteral("Fern"), QString(), t0 };
        plantRepo.add(p);
        const SensorId bound = sensorRepo.ensure(HandleKind::Mac, QStringLiteral("AA"),
                                                 QStringLiteral("m"));
        const SensorId loose = sensorRepo.ensure(HandleKind::Mac, QStringLiteral("BB"),
                                                 QStringLiteral("m"));
        bindingRepo.bind(p.id, bound, t0, std::nullopt); // `bound` is open-bound; `loose` is free
        registered.refresh();
        QCOMPARE(registered.rowCount(), 2);

        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &clock, nullptr, nullptr,
                       &sensorRepo, &readingRepo, nullptr, nullptr, &registered, &deleter,
                       &bindingRepo);
        QSignalSpy spy(&ctx, &AppContext::sensorRemoved);

        // The bound sensor is refused; it stays registered.
        ctx.removeRegisteredSensor(bound.toString());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toBool(), false);
        QVERIFY(sensorRepo.get(bound).has_value());
        QCOMPARE(registered.rowCount(), 2);

        // The unbound sensor is deleted and drops out of the registered list.
        ctx.removeRegisteredSensor(loose.toString());
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(0).toBool(), true);
        QVERIFY(!sensorRepo.get(loose).has_value());
        QCOMPARE(registered.rowCount(), 1);
    }

    // The sensor-detail page shows when this install last completed a history download for the
    // selected sensor — empty until the first sync, a relative text afterwards.
    void lastHistorySyncSurfacesOnSelectedSensor()
    {
        const QDateTime now(QDate(2026, 6, 1), QTime(12, 0), QTimeZone::UTC);
        FakeClock clock;
        clock.t = now.toMSecsSinceEpoch();

        InMemoryPlantRepository plantRepo;
        InMemoryJournalRepository journalRepo;
        InMemorySensorRepository sensorRepo(clock);
        InMemoryReadingRepository readingRepo;
        InMemorySyncStateRepository syncStateRepo;
        PlantListModel plants(plantRepo);
        PlantJournalModel journal(journalRepo);

        const SensorId s = sensorRepo.ensure(HandleKind::Mac, QStringLiteral("AA:BB"),
                                             QStringLiteral("FlowerCare"));

        AppContext ctx(nullptr, nullptr, nullptr, &plantRepo, &journalRepo, &plants, &journal,
                       nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr, nullptr, &clock, nullptr, nullptr,
                       &sensorRepo, &readingRepo, &syncStateRepo);

        // Never synced -> no "last synced" text.
        ctx.selectDevice(QStringLiteral("AA:BB"));
        QVERIFY(ctx.selectedLastSyncText().isEmpty());

        // After a completed sync, the detail page surfaces a relative timestamp.
        syncStateRepo.setLastHistorySync(s, now.addSecs(-120));
        ctx.selectDevice(QStringLiteral("AA:BB")); // re-select -> refreshSelectedStatus
        QVERIFY(!ctx.selectedLastSyncText().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAppContext)
#include "test_appcontext.moc"
