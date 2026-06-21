// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "attachment.h"
#include "backupimporter.h"
#include "backupserializer.h"
#include "carestatus.h"
#include "clock.h"
#include "inmemoryattachmentrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "journalentry.h"
#include "plant.h"
#include "reading.h"
#include "storageerror.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>

using namespace klr;

namespace {

// One bundle of the six in-memory repositories, with the serialize/restore helpers wired
// to a shared clock — so a test can seed, serialize, restore into a fresh bundle, and
// compare. Byte-equal re-serialization is the strongest "all entities equal" assertion:
// it covers every UUID, binding window, optional/null field and the canonical units.
struct RepoSet {
    InMemoryPlantRepository plants;
    InMemorySensorRepository sensors;
    InMemoryBindingRepository bindings;
    InMemoryReadingRepository readings;
    InMemoryJournalRepository journal;
    InMemoryCareThresholdRepository thresholds;
    InMemoryAttachmentRepository attachments;

    explicit RepoSet(const Clock &clock) : sensors(clock) {}

    QByteArray serialize(const Clock &clock)
    {
        return BackupSerializer(plants, sensors, bindings, readings, journal, thresholds, clock,
                                &attachments)
            .toJson();
    }
    BackupImporter::Result restore(const QByteArray &json, const Clock &clock)
    {
        return BackupImporter(plants, sensors, bindings, readings, journal, thresholds, clock,
                              &attachments)
            .importFrom(json);
    }
};

const QDateTime kT0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);

// A representative dataset: a plant whose sensor was swapped (one closed binding + one
// open, role-pinned), readings on both sensors (incl. an absent value), a journal entry,
// and a threshold.
void seed(RepoSet &s)
{
    Plant orchid { PlantId::generate(), QStringLiteral("Orchid, white"), QStringLiteral("Phalaenopsis"), kT0 };
    s.plants.add(orchid);

    const SensorId a = s.sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("OldProbe"));
    const SensorId b = s.sensors.ensure(HandleKind::Mac, QStringLiteral("BB"), QStringLiteral("NewProbe"));
    const QDateTime swap = kT0.addDays(30);

    s.bindings.bind(orchid.id, a, kT0, std::nullopt);
    s.bindings.unbind(orchid.id, a, swap);
    s.bindings.bind(orchid.id, b, swap, Quantity::SoilMoisture); // role-pinned, open

    s.readings.append(a, std::array{
        Reading{ Quantity::SoilMoisture, 55.0, Unit::Percent, kT0, Provenance::History } });
    s.readings.append(b, std::array{
        Reading{ Quantity::SoilMoisture, 50.0, Unit::Percent, kT0.addDays(60), Provenance::History },
        Reading{ Quantity::SoilMoisture, std::nullopt, Unit::Percent, kT0.addDays(61),
                 Provenance::History } });

    const JournalEntryId repot = JournalEntryId::generate();
    s.journal.add(JournalEntry{ repot, orchid.id, kT0.addDays(5),
                                JournalEntryKind::Repotting, QStringLiteral("new pot") });
    // Two photos on the repot entry — metadata only (ADR 0024); the byte-identical re-serialize
    // check proves they round-trip (id, entry FK, fileRef, caption, addedAt).
    s.attachments.add(Attachment{ AttachmentId::generate(), repot,
                                  QStringLiteral("attachments/before.jpg"),
                                  QStringLiteral("Before"), kT0.addDays(5) });
    s.attachments.add(Attachment{ AttachmentId::generate(), repot,
                                  QStringLiteral("attachments/after.jpg"),
                                  QStringLiteral("After"), kT0.addDays(5).addSecs(60) });
    // A second entry that was later edited — its editedAt must round-trip (ADR 0020).
    JournalEntry edited{ JournalEntryId::generate(), orchid.id, kT0.addDays(6),
                         JournalEntryKind::Note, QStringLiteral("amended") };
    edited.editedAt = kT0.addDays(8);
    s.journal.add(edited);
    // A global (plant-less) entry — user-wide agent memory (ADR 0022). It must round-trip even
    // though it is not reached by the per-plant walk.
    s.journal.add(JournalEntry{ JournalEntryId::generate(), std::nullopt, kT0.addDays(7),
                                JournalEntryKind::Memory, QStringLiteral("hard tap water") });
    s.thresholds.setRange(orchid.id, CareRange{ Quantity::SoilMoisture, 20.0, 60.0 });
}

} // namespace

class TestBackupImport : public QObject {
    Q_OBJECT

    FakeClock m_clock; // shared so exportedAt matches across serialize calls

private slots:
    void roundTripReproducesEverything()
    {
        RepoSet src(m_clock);
        seed(src);
        const QByteArray backup = src.serialize(m_clock);

        RepoSet dst(m_clock);
        const BackupImporter::Result r = dst.restore(backup, m_clock);

        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(r.plants, 1);
        QCOMPARE(r.sensors, 2);
        QCOMPARE(r.bindings, 2);
        QCOMPARE(r.thresholds, 1);
        QCOMPARE(r.journal, 3); // 2 plant entries + 1 global (ADR 0022)
        QCOMPARE(r.attachments, 2); // two photos on the repot entry (ADR 0024)

        // Headline: re-serializing the restored set is byte-identical — every UUID,
        // binding window, role, optional/null field and canonical unit survived.
        QCOMPARE(dst.serialize(m_clock), backup);

        // The global entry survived (it is plant-less, so not reached via forPlant).
        const QList<JournalEntry> globals = dst.journal.globalEntries();
        QCOMPARE(globals.size(), 1);
        QVERIFY(!globals.first().plant.has_value());
        QCOMPARE(globals.first().kind, JournalEntryKind::Memory);
        QCOMPARE(globals.first().note, QStringLiteral("hard tap water"));

        // Explicit editedAt round-trip: exactly one entry was edited; the other never was.
        const QList<JournalEntry> entries = dst.journal.forPlant(src.plants.all().first().id);
        QCOMPARE(entries.size(), 2);
        int edited = 0, never = 0;
        for (const JournalEntry &e : entries) {
            if (e.editedAt) {
                ++edited;
                QCOMPARE(*e.editedAt, kT0.addDays(8));
            } else {
                ++never;
            }
        }
        QCOMPARE(edited, 1);
        QCOMPARE(never, 1);
    }

    void oldBackupWithoutTsEditedImportsAsNeverEdited()
    {
        RepoSet src(m_clock);
        seed(src);
        QJsonObject root = QJsonDocument::fromJson(src.serialize(m_clock)).object();

        // Simulate a backup taken before ADR 0020: strip every tsEdited field.
        QJsonArray journal = root.value(QStringLiteral("journal")).toArray();
        for (int i = 0; i < journal.size(); ++i) {
            QJsonObject e = journal.at(i).toObject();
            e.remove(QStringLiteral("tsEdited"));
            journal.replace(i, e);
        }
        root.insert(QStringLiteral("journal"), journal);

        RepoSet dst(m_clock);
        const BackupImporter::Result r = dst.restore(QJsonDocument(root).toJson(), m_clock);
        QVERIFY(r.warnings.isEmpty());
        const QList<JournalEntry> entries = dst.journal.forPlant(src.plants.all().first().id);
        QCOMPARE(entries.size(), 2);
        for (const JournalEntry &e : entries)
            QVERIFY(!e.editedAt.has_value()); // absent tsEdited -> never edited
    }

    void restoreIsIdempotent()
    {
        RepoSet src(m_clock);
        seed(src);
        const QByteArray backup = src.serialize(m_clock);

        RepoSet dst(m_clock);
        dst.restore(backup, m_clock);
        dst.restore(backup, m_clock); // second time: must not duplicate anything

        QCOMPARE(dst.plants.all().size(), 1);
        QCOMPARE(dst.sensors.all().size(), 2);
        QCOMPARE(dst.bindings.bindings(src.plants.all().first().id).size(), 2);
        QCOMPARE(dst.journal.forPlant(src.plants.all().first().id).size(), 2);
        QCOMPARE(dst.serialize(m_clock), backup); // still identical
    }

    void unknownEnumTokenWarnsAndSkipsRow()
    {
        RepoSet src(m_clock);
        seed(src);
        QJsonObject root = QJsonDocument::fromJson(src.serialize(m_clock)).object();

        // Corrupt one sensor's handleKind to a token this build does not know.
        QJsonArray sensors = root.value(QStringLiteral("sensors")).toArray();
        QJsonObject s0 = sensors.at(0).toObject();
        s0.insert(QStringLiteral("handleKind"), QStringLiteral("Telepathy"));
        sensors.replace(0, s0);
        root.insert(QStringLiteral("sensors"), sensors);

        RepoSet dst(m_clock);
        const BackupImporter::Result r = dst.restore(QJsonDocument(root).toJson(), m_clock);
        QVERIFY(!r.warnings.isEmpty());     // the bad row was flagged
        QCOMPARE(r.sensors, 1);             // only the good sensor imported
        QCOMPARE(dst.sensors.all().size(), 1);
        QCOMPARE(dst.plants.all().size(), 1); // unrelated rows still imported
    }

    void newerFormatVersionIsRefused()
    {
        RepoSet src(m_clock);
        seed(src);
        QJsonObject root = QJsonDocument::fromJson(src.serialize(m_clock)).object();
        root.insert(QStringLiteral("formatVersion"), BackupSerializer::kFormatVersion + 1);

        RepoSet dst(m_clock);
        bool threw = false;
        try {
            dst.restore(QJsonDocument(root).toJson(), m_clock);
        } catch (const StorageError &e) {
            threw = true;
            QVERIFY(QString::fromUtf8(e.what()).contains(QStringLiteral("newer")));
        }
        QVERIFY(threw);
        QCOMPARE(dst.plants.all().size(), 0); // nothing imported
    }

    void garbageInputIsRefused()
    {
        RepoSet dst(m_clock);
        QVERIFY_THROWS_EXCEPTION(StorageError, dst.restore(QByteArrayLiteral("not json"), m_clock));
        QVERIFY_THROWS_EXCEPTION(StorageError,
                                 dst.restore(QByteArrayLiteral("{\"hello\":1}"), m_clock));
    }
};

QTEST_GUILESS_MAIN(TestBackupImport)
#include "test_backupimport.moc"
