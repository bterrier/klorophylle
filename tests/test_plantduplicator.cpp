// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h" // CareRange
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "plant.h"
#include "plantduplicator.h"
#include "reading.h"
#include "storageerror.h"

#include <span>

using namespace klr;

namespace {
// A journal repo whose add() always fails — to drive the duplicator's compensating
// cleanup (a write fails AFTER the plant row exists). InMemoryJournalRepository is final,
// so implement the interface directly.
class ThrowingJournalRepository final : public IJournalRepository {
public:
    void add(const JournalEntry &) override
    {
        throw StorageError(QStringLiteral("simulated journal write failure"));
    }
    void update(const JournalEntry &) override {}
    void remove(JournalEntryId) override {}
    // Return one entry so the duplicator's copy loop runs and hits the failing add().
    QList<JournalEntry> forPlant(PlantId plant) const override
    {
        return { JournalEntry{ JournalEntryId::generate(), plant, QDateTime::currentDateTimeUtc(),
                               JournalEntryKind::Note, QStringLiteral("x") } };
    }
    QList<JournalEntry> globalEntries() const override { return {}; }
};
} // namespace

// PlantDuplicator: clones a plant so a once-shared pot can be split into two tracked
// plants that each keep the FULL shared history. The contract is that history (which
// follows the plant THROUGH its binding windows, ADR 0005/0006) is preserved by copying
// the binding history verbatim — never by copying readings. Exercised against the
// in-memory fakes (same behaviour as the SQLite repos).
class TestPlantDuplicator : public QObject {
    Q_OBJECT

    static QDateTime utc(int day, int hour = 0)
    {
        return QDateTime(QDate(2026, 1, day), QTime(hour, 0), QTimeZone::UTC);
    }

    static Reading soil(double v, const QDateTime &ts)
    {
        return Reading{ Quantity::SoilMoisture, v, canonicalUnit(Quantity::SoilMoisture), ts,
                        Provenance::History };
    }

    // A plant A with species, trackedSince, thresholds, journal, and one open binding to a
    // sensor that has accumulated soil-moisture readings.
    struct Fixture {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        PlantId a;
        SensorId s1{ SensorId::generate() };

        Fixture()
        {
            Plant p;
            p.id = PlantId::generate();
            p.displayName = QStringLiteral("Shared pot");
            p.species = QStringLiteral("Ocimum basilicum");
            p.trackedSince = utc(1);
            plants.add(p);
            a = p.id;

            thresholds.setRange(a, CareRange{ Quantity::SoilMoisture, 20.0, 60.0 });
            thresholds.setRange(a, CareRange{ Quantity::AirTemperature, 15.0, 30.0 });

            journal.add(JournalEntry{ JournalEntryId::generate(), a, utc(2, 9),
                                      JournalEntryKind::Watering, QStringLiteral("watered") });
            // One entry was later edited — its editedAt must clone verbatim (ADR 0020).
            JournalEntry edited{ JournalEntryId::generate(), a, utc(3, 9),
                                 JournalEntryKind::Note, QStringLiteral("looking good") };
            edited.editedAt = utc(4, 9);
            journal.add(edited);

            bindings.bind(a, s1, utc(1), std::nullopt); // open
            const Reading r[] = { soil(40.0, utc(2, 10)), soil(45.0, utc(3, 10)),
                                  soil(42.0, utc(4, 10)) };
            readings.append(s1, std::span<const Reading>(r, std::size(r)));
        }
    };

private slots:
    void clonesScalarFieldsWithFreshId()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("Shared pot (copy)"));

        QVERIFY(!(copy == f.a)); // fresh identity
        const std::optional<Plant> cp = f.plants.get(copy);
        QVERIFY(cp.has_value());
        QCOMPARE(cp->displayName, QStringLiteral("Shared pot (copy)"));
        QCOMPARE(cp->species, QStringLiteral("Ocimum basilicum")); // species carries over
        QCOMPARE(cp->trackedSince, utc(1));                        // timeline preserved
        QCOMPARE(f.plants.all().size(), 2);                        // original still there
    }

    void copiesThresholds()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("copy"));

        auto byQuantity = [](QList<CareRange> r) {
            std::sort(r.begin(), r.end(), [](const CareRange &x, const CareRange &y) {
                return static_cast<int>(x.quantity) < static_cast<int>(y.quantity);
            });
            return r;
        };
        QCOMPARE(byQuantity(f.thresholds.thresholdsFor(copy)),
                 byQuantity(f.thresholds.thresholdsFor(f.a)));
    }

    void copiesJournalWithDistinctIds()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("copy"));

        const QList<JournalEntry> src = f.journal.forPlant(f.a);
        const QList<JournalEntry> cp = f.journal.forPlant(copy);
        QCOMPARE(cp.size(), src.size());
        for (int i = 0; i < cp.size(); ++i) {
            QVERIFY(!(cp[i].id == src[i].id)); // fresh entry identity
            QCOMPARE(cp[i].plant, copy);       // re-homed to the duplicate
            QCOMPARE(cp[i].timestamp, src[i].timestamp);
            QCOMPARE(cp[i].kind, src[i].kind);
            QCOMPARE(cp[i].note, src[i].note);
            QCOMPARE(cp[i].editedAt, src[i].editedAt); // edit history preserved verbatim
        }
        // The fixture seeds exactly one edited entry; confirm the clone actually carries it.
        QVERIFY(std::any_of(cp.cbegin(), cp.cend(),
                            [](const JournalEntry &e) { return e.editedAt.has_value(); }));
    }

    void copiesBindingHistoryStructurally()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("copy"));

        const QList<PlantSensorBinding> src = f.bindings.bindings(f.a);
        const QList<PlantSensorBinding> cp = f.bindings.bindings(copy);
        QCOMPARE(cp.size(), src.size());
        for (int i = 0; i < cp.size(); ++i) {
            QCOMPARE(cp[i].plant, copy); // owned by the duplicate
            QCOMPARE(cp[i].sensor, src[i].sensor);
            QCOMPARE(cp[i].validFrom, src[i].validFrom);
            QCOMPARE(cp[i].validTo, src[i].validTo);
            QCOMPARE(cp[i].role, src[i].role);
        }
    }

    // The core guarantee: the duplicate resolves the SAME reading series as the original
    // (history followed the plant through its copied bindings — no readings were copied).
    void duplicateResolvesSameHistory()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("copy"));

        const QList<PlantSensorBinding> ba = f.bindings.bindings(f.a);
        const QList<PlantSensorBinding> bc = f.bindings.bindings(copy);
        const QList<Reading> sa = f.readings.seriesForPlant(
            std::span<const PlantSensorBinding>(ba.constData(), ba.size()), Quantity::SoilMoisture,
            utc(1), utc(10));
        const QList<Reading> sc = f.readings.seriesForPlant(
            std::span<const PlantSensorBinding>(bc.constData(), bc.size()), Quantity::SoilMoisture,
            utc(1), utc(10));

        QCOMPARE(sc.size(), sa.size());
        QVERIFY(sc.size() >= 3);
        for (int i = 0; i < sc.size(); ++i) {
            QCOMPARE(sc[i].timestamp, sa[i].timestamp);
            QCOMPARE(sc[i].value, sa[i].value);
        }
    }

    // The split: detaching the shared sensor from the duplicate "now" closes its binding
    // window but keeps the past — the duplicate retains the full pre-split history, and the
    // original is untouched (its binding stays open).
    void detachAfterDuplicateKeepsPastAndLeavesOriginal()
    {
        Fixture f;
        PlantDuplicator dup(f.plants, f.journal, f.thresholds, f.bindings);
        const PlantId copy = dup.duplicate(f.a, QStringLiteral("copy"));

        const QDateTime splitAt = utc(5);
        f.bindings.unbind(copy, f.s1, splitAt);

        // The duplicate's window is now closed; its history up to the split is intact.
        const QList<PlantSensorBinding> bc = f.bindings.bindings(copy);
        QCOMPARE(bc.size(), 1);
        QCOMPARE(bc.first().validTo, std::optional<QDateTime>(splitAt));
        const QList<Reading> sc = f.readings.seriesForPlant(
            std::span<const PlantSensorBinding>(bc.constData(), bc.size()), Quantity::SoilMoisture,
            utc(1), utc(10));
        QCOMPARE(sc.size(), 3); // all three pre-split readings (utc 2,3,4 < splitAt)

        // The original is unaffected: still bound (open) and still reads its full history.
        const QList<PlantSensorBinding> ba = f.bindings.bindings(f.a);
        QCOMPARE(ba.size(), 1);
        QCOMPARE(ba.first().validTo, std::optional<QDateTime>(std::nullopt));
    }

    // Chronological replay must preserve a closed-then-reopened window (a real swap-back).
    void replaysClosedThenReopenedBinding()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;
        InMemoryBindingRepository bindings;

        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("A");
        p.trackedSince = utc(1);
        plants.add(p);

        const SensorId s{ SensorId::generate() };
        bindings.bind(p.id, s, utc(1), std::nullopt);
        bindings.unbind(p.id, s, utc(3)); // closed [1,3)
        bindings.bind(p.id, s, utc(5), std::nullopt); // reopened [5, open)

        PlantDuplicator dup(plants, journal, thresholds, bindings);
        const PlantId copy = dup.duplicate(p.id, QStringLiteral("A (copy)"));

        const QList<PlantSensorBinding> bc = bindings.bindings(copy);
        QCOMPARE(bc.size(), 2);
        QCOMPARE(bc[0].validFrom, utc(1));
        QCOMPARE(bc[0].validTo, std::optional<QDateTime>(utc(3)));
        QCOMPARE(bc[1].validFrom, utc(5));
        QCOMPARE(bc[1].validTo, std::optional<QDateTime>(std::nullopt));
    }

    // A write failing mid-clone must leave NO partial plant behind (compensating cleanup):
    // the plant row added first is removed when a later step throws.
    void failureMidwayLeavesNoPartialPlant()
    {
        Fixture f; // seeds one plant (A) + thresholds + journal + binding
        ThrowingJournalRepository failingJournal;
        PlantDuplicator dup(f.plants, failingJournal, f.thresholds, f.bindings);

        QVERIFY_EXCEPTION_THROWN(dup.duplicate(f.a, QStringLiteral("copy")), StorageError);

        // Only the original remains — the half-made copy was rolled back.
        QCOMPARE(f.plants.all().size(), 1);
        QCOMPARE(f.plants.all().first().id, f.a);
    }

    void missingSourceThrows()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;
        InMemoryBindingRepository bindings;
        PlantDuplicator dup(plants, journal, thresholds, bindings);
        QVERIFY_EXCEPTION_THROWN(dup.duplicate(PlantId::generate(), QStringLiteral("x")),
                                 std::exception);
    }
};

QTEST_GUILESS_MAIN(TestPlantDuplicator)
#include "test_plantduplicator.moc"
