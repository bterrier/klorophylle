// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "backupserializer.h"
#include "carestatus.h"
#include "clock.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "journalentry.h"
#include "plant.h"
#include "reading.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimeZone>

using namespace klr;

// BackupSerializer produces a stable, canonical JSON shape (ADR 0010). Deterministic via
// FakeClock; seeded from the in-memory repos. The headline round-trip (serialize ->
// import -> equal) lives in test_backupimport — here we pin the wire shape itself.
class TestBackupSerialize : public QObject {
    Q_OBJECT

    const QDateTime kT0 { QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC };

private slots:
    void shapeAndTokensAndNulls()
    {
        FakeClock clock;
        clock.t = QDateTime(QDate(2026, 6, 9), QTime(12, 3, 0), QTimeZone::UTC).toMSecsSinceEpoch();

        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;

        Plant fern { PlantId::generate(), QStringLiteral("Fern"), QStringLiteral("Nephrolepis"), kT0 };
        plants.add(fern);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB"),
                                          QStringLiteral("FlowerCare"));
        // Open binding (validTo null) with an explicit role.
        bindings.bind(fern.id, s, kT0, Quantity::AirTemperature);
        readings.append(s, std::array{
            Reading{ Quantity::AirTemperature, 21.5, Unit::DegreeCelsius, kT0, Provenance::History },
            Reading{ Quantity::AirTemperature, std::nullopt, Unit::DegreeCelsius,
                     kT0.addSecs(3600), Provenance::History } });
        journal.add(JournalEntry{ JournalEntryId::generate(), fern.id, kT0,
                                  JournalEntryKind::Watering, QStringLiteral("gave water") });
        thresholds.setRange(fern.id, CareRange{ Quantity::AirTemperature, 15.0, 30.0 });

        BackupSerializer serializer(plants, sensors, bindings, readings, journal, thresholds, clock);
        const QJsonObject root = QJsonDocument::fromJson(serializer.toJson()).object();

        QCOMPARE(root.value(QStringLiteral("formatVersion")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("app")).toString(), QStringLiteral("klorophylle"));
        QCOMPARE(root.value(QStringLiteral("exportedAt")).toString(),
                 QStringLiteral("2026-06-09T12:03:00Z"));

        const QJsonObject p = root.value(QStringLiteral("plants")).toArray().first().toObject();
        QCOMPARE(p.value(QStringLiteral("id")).toString(), fern.id.toString());
        QCOMPARE(p.value(QStringLiteral("displayName")).toString(), QStringLiteral("Fern"));
        QCOMPARE(p.value(QStringLiteral("species")).toString(), QStringLiteral("Nephrolepis"));

        const QJsonObject sj = root.value(QStringLiteral("sensors")).toArray().first().toObject();
        QCOMPARE(sj.value(QStringLiteral("handleKind")).toString(), QStringLiteral("Mac"));
        QCOMPARE(sj.value(QStringLiteral("model")).toString(), QStringLiteral("FlowerCare"));

        const QJsonObject b = root.value(QStringLiteral("bindings")).toArray().first().toObject();
        QCOMPARE(b.value(QStringLiteral("role")).toString(), QStringLiteral("AirTemperature"));
        QVERIFY(b.value(QStringLiteral("validTo")).isNull()); // open binding
        QCOMPARE(b.value(QStringLiteral("sensor")).toString(), s.toString());

        const QJsonArray rs = root.value(QStringLiteral("readings")).toArray();
        QCOMPARE(rs.size(), 2);
        QCOMPARE(rs.at(0).toObject().value(QStringLiteral("quantity")).toString(),
                 QStringLiteral("AirTemperature"));
        QCOMPARE(rs.at(0).toObject().value(QStringLiteral("value")).toDouble(), 21.5);
        QVERIFY(rs.at(1).toObject().value(QStringLiteral("value")).isNull()); // absent -> null

        const QJsonObject e = root.value(QStringLiteral("journal")).toArray().first().toObject();
        QCOMPARE(e.value(QStringLiteral("kind")).toString(), QStringLiteral("Watering"));
        QCOMPARE(e.value(QStringLiteral("note")).toString(), QStringLiteral("gave water"));
        QVERIFY(!e.contains(QStringLiteral("tsEdited"))); // never edited -> field omitted

        const QJsonObject t = root.value(QStringLiteral("thresholds")).toArray().first().toObject();
        QCOMPARE(t.value(QStringLiteral("quantity")).toString(), QStringLiteral("AirTemperature"));
        QCOMPARE(t.value(QStringLiteral("min")).toDouble(), 15.0);
        QCOMPARE(t.value(QStringLiteral("max")).toDouble(), 30.0);
    }

    void editedEntryWritesTsEdited()
    {
        FakeClock clock;
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;

        Plant fern { PlantId::generate(), QStringLiteral("Fern"), QStringLiteral("Nephrolepis"), kT0 };
        plants.add(fern);
        JournalEntry e { JournalEntryId::generate(), fern.id, kT0,
                         JournalEntryKind::Note, QStringLiteral("typo") };
        e.editedAt = kT0.addSecs(7200);
        journal.add(e);

        BackupSerializer serializer(plants, sensors, bindings, readings, journal, thresholds, clock);
        const QJsonObject root = QJsonDocument::fromJson(serializer.toJson()).object();
        const QJsonObject jo = root.value(QStringLiteral("journal")).toArray().first().toObject();
        QCOMPARE(jo.value(QStringLiteral("tsEdited")).toString(),
                 QStringLiteral("2026-01-01T02:00:00Z"));
    }

    void emptyDatasetHasEmptyArrays()
    {
        FakeClock clock;
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryJournalRepository journal;
        InMemoryCareThresholdRepository thresholds;

        BackupSerializer serializer(plants, sensors, bindings, readings, journal, thresholds, clock);
        const QJsonObject root = QJsonDocument::fromJson(serializer.toJson()).object();
        QVERIFY(root.value(QStringLiteral("plants")).toArray().isEmpty());
        QVERIFY(root.value(QStringLiteral("readings")).toArray().isEmpty());
        QCOMPARE(root.value(QStringLiteral("formatVersion")).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(TestBackupSerialize)
#include "test_backupserialize.moc"
