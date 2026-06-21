// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "agenttools.h"
#include "attachment.h"
#include "carestatus.h"
#include "clock.h"
#include "fakewebfetcher.h"
#include "inmemoryattachmentfilestore.h"
#include "inmemoryattachmentrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "journalentry.h"
#include "message.h" // karness::TextBlock / ContentPart
#include "plant.h"
#include "reading.h"
#include "webcontent.h"

#include <QtCore/QJsonArray>

using namespace klr;
using karness::TextBlock;
using karness::ToolOutcome;

// The agent's domain tools (docs/adr/0019), exercised on the in-memory fakes +
// FakeClock. Tools return ready futures, so result() resolves without an event loop.
namespace {

constexpr qint64 kNowMs = 1737331200000LL; // 2025-01-20T00:00:00Z

Plant plantTracked(const QString &name, const QString &species, qint64 trackedMs)
{
    return Plant{PlantId::generate(), name, species,
                 QDateTime::fromMSecsSinceEpoch(trackedMs, QTimeZone::UTC)};
}

// The single text part of a tool outcome (the tools only ever emit one TextBlock).
QString text(const ToolOutcome &o)
{
    Q_ASSERT(o.parts.size() == 1);
    return std::get<TextBlock>(o.parts.first()).text;
}

} // namespace

class TestAgentTools : public QObject
{
    Q_OBJECT

private slots:
    void listPlantsSpec()
    {
        InMemoryPlantRepository plants;
        FakeClock clock;
        ListPlantsTool tool(plants, clock);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("list_plants"));
        QVERIFY(!spec.description.isEmpty());
        // No-argument tool: object schema with empty properties.
        QCOMPARE(spec.inputSchema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
        QVERIFY(spec.inputSchema.value(QStringLiteral("properties")).toObject().isEmpty());
    }

    void listPlantsEmpty()
    {
        InMemoryPlantRepository plants;
        FakeClock clock;
        clock.t = kNowMs;
        ListPlantsTool tool(plants, clock);

        const ToolOutcome o = tool.invoke({}).result();
        QVERIFY(!o.isError);
        QCOMPARE(text(o), QStringLiteral("No plants are being tracked yet."));
    }

    void listPlantsRosterSortedWithIds()
    {
        InMemoryPlantRepository plants;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QStringLiteral("Ocimum basilicum"),
                                         kNowMs - 30LL * 24 * 60 * 60 * 1000);
        const Plant fern = plantTracked(QStringLiteral("Fern"), QString(),
                                        kNowMs - 1LL * 24 * 60 * 60 * 1000);
        plants.add(fern); // insert newest first; tool must sort oldest-first
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        ListPlantsTool tool(plants, clock);

        const ToolOutcome o = tool.invoke({}).result();
        QVERIFY(!o.isError);
        const QString expected = QStringLiteral("2 plants:\n")
            + QStringLiteral("- Basil — species Ocimum basilicum — tracked 30 days — id %1\n")
                  .arg(basil.id.toString())
            + QStringLiteral("- Fern — species unknown — tracked 1 day — id %1\n").arg(fern.id.toString());
        QCOMPARE(text(o), expected);
    }

    // --- read_plant_journal ---

    void journalSpecRequiresPlantId()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        ReadPlantJournalTool tool(plants, journal);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("read_plant_journal"));
        const auto required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QCOMPARE(required.first().toString(), QStringLiteral("plant_id"));
    }

    void journalMissingPlantIdIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        ReadPlantJournalTool tool(plants, journal);

        const ToolOutcome o = tool.invoke({}).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("plant_id")));
    }

    void journalUnknownPlantIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        ReadPlantJournalTool tool(plants, journal);

        const QJsonObject args{{QStringLiteral("plant_id"), PlantId::generate().toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("no plant")));
    }

    void journalEmpty()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        ReadPlantJournalTool tool(plants, journal);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);
        QCOMPARE(text(o), QStringLiteral("Basil has no journal entries."));
    }

    void journalNewestFirstWithLimit()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);

        // Three entries at increasing timestamps; forPlant returns newest-first.
        const QDateTime t1 = QDateTime::fromMSecsSinceEpoch(kNowMs - 3LL * 86400000, QTimeZone::UTC);
        const QDateTime t2 = QDateTime::fromMSecsSinceEpoch(kNowMs - 2LL * 86400000, QTimeZone::UTC);
        const QDateTime t3 = QDateTime::fromMSecsSinceEpoch(kNowMs - 1LL * 86400000, QTimeZone::UTC);
        journal.add(JournalEntry{JournalEntryId::generate(), basil.id, t1, JournalEntryKind::Note,
                                 QStringLiteral("bought it")});
        journal.add(JournalEntry{JournalEntryId::generate(), basil.id, t2, JournalEntryKind::Watering,
                                 QStringLiteral("deep soak")});
        journal.add(JournalEntry{JournalEntryId::generate(), basil.id, t3,
                                 JournalEntryKind::Observation, QString()});

        ReadPlantJournalTool tool(plants, journal);
        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("limit"), 2}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);

        const QString expected =
            QStringLiteral("Basil — 2 of 3 journal entries (newest first):\n")
            + QStringLiteral("- %1 — Observation — (no note)\n").arg(t3.toUTC().toString(Qt::ISODate))
            + QStringLiteral("- %1 — Watering — deep soak\n").arg(t2.toUTC().toString(Qt::ISODate));
        QCOMPARE(text(o), expected);
    }

    // --- read_plant_data ---

    void dataSpecRequiresPlantId()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        FakeClock clock;
        ReadPlantDataTool tool(plants, bindings, readings, thresholds, clock);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("read_plant_data"));
        const auto required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QCOMPARE(required.first().toString(), QStringLiteral("plant_id"));
    }

    void dataUnknownPlantIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        FakeClock clock;
        clock.t = kNowMs;
        ReadPlantDataTool tool(plants, bindings, readings, thresholds, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), PlantId::generate().toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
    }

    void dataNoReadings()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        ReadPlantDataTool tool(plants, bindings, readings, thresholds, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);
        QVERIFY(text(o).contains(QStringLiteral("no current sensor readings")));
    }

    void dataCurrentWithThresholdStatus()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);

        const SensorId sensor = SensorId::generate();
        const QDateTime from = QDateTime::fromMSecsSinceEpoch(kNowMs - 30LL * 86400000, QTimeZone::UTC);
        bindings.bind(basil.id, sensor, from, std::nullopt);
        const QDateTime t = QDateTime::fromMSecsSinceEpoch(kNowMs - 3600000, QTimeZone::UTC);
        const QList<Reading> rows{
            Reading{Quantity::SoilMoisture, 42.0, Unit::Percent, t, Provenance::Live},
            Reading{Quantity::AirTemperature, 31.0, Unit::DegreeCelsius, t, Provenance::Live},
            Reading{Quantity::Battery, 88.0, Unit::Percent, t, Provenance::Live}};
        readings.append(sensor, rows);

        thresholds.setRange(basil.id, CareRange{Quantity::SoilMoisture, 30.0, 60.0});
        thresholds.setRange(basil.id, CareRange{Quantity::AirTemperature, 18.0, 28.0});
        // Battery: intentionally no threshold.

        FakeClock clock;
        clock.t = kNowMs;
        ReadPlantDataTool tool(plants, bindings, readings, thresholds, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);
        const QString s = text(o);
        QVERIFY(s.contains(QStringLiteral("42.0 %")));
        QVERIFY(s.contains(QStringLiteral("(ideal; range 30.0")));
        QVERIFY(s.contains(QStringLiteral("31.0")));
        QVERIFY(s.contains(QStringLiteral("(above ideal; range 18.0")));
        QVERIFY(s.contains(QStringLiteral("Battery: 88.0 % (no threshold set)")));
        // Ordered by quantity ordinal: soil moisture before temperature before battery
        // (label(AirTemperature) == "Temperature").
        QVERIFY(s.indexOf(QStringLiteral("Soil moisture")) < s.indexOf(QStringLiteral("Temperature")));
        QVERIFY(s.indexOf(QStringLiteral("Temperature")) < s.indexOf(QStringLiteral("Battery")));
    }

    void dataWindowMinAvgMax()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);

        const SensorId sensor = SensorId::generate();
        bindings.bind(basil.id, sensor,
                      QDateTime::fromMSecsSinceEpoch(kNowMs - 30LL * 86400000, QTimeZone::UTC),
                      std::nullopt);
        // Three soil-moisture samples over the last 3 days: 38, 42, 45 → min 38, avg 41.7, max 45.
        const auto at = [](qint64 daysAgo) {
            return QDateTime::fromMSecsSinceEpoch(kNowMs - daysAgo * 86400000, QTimeZone::UTC);
        };
        readings.append(sensor, QList<Reading>{
            Reading{Quantity::SoilMoisture, 38.0, Unit::Percent, at(3), Provenance::Live},
            Reading{Quantity::SoilMoisture, 42.0, Unit::Percent, at(2), Provenance::Live},
            Reading{Quantity::SoilMoisture, 45.0, Unit::Percent, at(1), Provenance::Live}});

        FakeClock clock;
        clock.t = kNowMs;
        ReadPlantDataTool tool(plants, bindings, readings, thresholds, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("window_days"), 7}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);
        const QString s = text(o);
        QVERIFY(s.contains(QStringLiteral("Last 7 days:")));
        QVERIFY(s.contains(QStringLiteral("min 38.0 %, avg 41.7 %, max 45.0 %")));
    }

    // --- add_journal_entry ---

    void addEntrySpecRequiresPlantIdAndNote()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        FakeClock clock;
        AddJournalEntryTool tool(plants, journal, clock);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("add_journal_entry"));
        const auto required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 2);
        QVERIFY(required.contains(QStringLiteral("plant_id")));
        QVERIFY(required.contains(QStringLiteral("note")));
        // kind advertises the valid enum.
        const auto kindEnum = spec.inputSchema.value(QStringLiteral("properties"))
                                  .toObject()
                                  .value(QStringLiteral("kind"))
                                  .toObject()
                                  .value(QStringLiteral("enum"))
                                  .toArray();
        QVERIFY(kindEnum.contains(QStringLiteral("Watering")));
    }

    void addEntryMissingNoteIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        AddJournalEntryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("note")));
        QVERIFY(journal.forPlant(basil.id).isEmpty()); // nothing written
    }

    void addEntryUnknownKindIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        AddJournalEntryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("note"), QStringLiteral("hi")},
                               {QStringLiteral("kind"), QStringLiteral("Composting")}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("unknown kind")));
        QVERIFY(journal.forPlant(basil.id).isEmpty());
    }

    void addEntryDefaultKindWrites()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        AddJournalEntryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("note"), QStringLiteral("looking healthy")}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);

        const QList<JournalEntry> entries = journal.forPlant(basil.id);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().kind, JournalEntryKind::Note); // default
        QCOMPARE(entries.first().note, QStringLiteral("looking healthy"));
        QCOMPARE(entries.first().timestamp,
                 QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC)); // clock-stamped
    }

    void addEntryExplicitKindWrites()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        AddJournalEntryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("note"), QStringLiteral("2 cups")},
                               {QStringLiteral("kind"), QStringLiteral("Watering")}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);

        const QList<JournalEntry> entries = journal.forPlant(basil.id);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().kind, JournalEntryKind::Watering);
    }

    // --- set_plant_memory ---

    void memorySpecRequiresPlantIdAndText()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        FakeClock clock;
        SetPlantMemoryTool tool(plants, journal, clock);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("set_plant_memory"));
        const auto required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 2);
        QVERIFY(required.contains(QStringLiteral("plant_id")));
        QVERIFY(required.contains(QStringLiteral("text")));
    }

    void memoryMissingTextIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        SetPlantMemoryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("text")));
        QVERIFY(journal.forPlant(basil.id).isEmpty()); // nothing written
    }

    void memoryUnknownPlantIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        FakeClock clock;
        clock.t = kNowMs;
        SetPlantMemoryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), PlantId::generate().toString()},
                               {QStringLiteral("text"), QStringLiteral("waters lightly")}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("no plant")));
    }

    void memoryFirstCallCreatesOneEntry()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        SetPlantMemoryTool tool(plants, journal, clock);

        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("text"), QStringLiteral("south-facing window")}};
        const ToolOutcome o = tool.invoke(args).result();
        QVERIFY(!o.isError);

        const QList<JournalEntry> entries = journal.forPlant(basil.id);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().kind, JournalEntryKind::Memory);
        QCOMPARE(entries.first().note, QStringLiteral("south-facing window"));
        QCOMPARE(entries.first().timestamp, QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC));
        // A creation, not an edit: editedAt stays nullopt (the fresh entry date floats it).
        QVERIFY(!entries.first().editedAt.has_value());
    }

    void memorySecondCallRewritesInPlaceFloatingBothDates()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        SetPlantMemoryTool tool(plants, journal, clock);

        const QJsonObject first{{QStringLiteral("plant_id"), basil.id.toString()},
                                {QStringLiteral("text"), QStringLiteral("waters lightly")}};
        QVERIFY(!tool.invoke(first).result().isError);
        const JournalEntryId firstId = journal.forPlant(basil.id).first().id;

        // A day later, rewrite the whole blob.
        const qint64 laterMs = kNowMs + 86400000;
        clock.t = laterMs;
        const QJsonObject second{{QStringLiteral("plant_id"), basil.id.toString()},
                                 {QStringLiteral("text"),
                                  QStringLiteral("waters lightly; sensitive to repotting")}};
        QVERIFY(!tool.invoke(second).result().isError);

        const QList<JournalEntry> entries = journal.forPlant(basil.id);
        QCOMPARE(entries.size(), 1);                       // still exactly one Memory entry
        QCOMPARE(entries.first().id, firstId);             // same stable id — an in-place rewrite
        QCOMPARE(entries.first().note, QStringLiteral("waters lightly; sensitive to repotting"));
        const QDateTime later = QDateTime::fromMSecsSinceEpoch(laterMs, QTimeZone::UTC);
        QCOMPARE(entries.first().timestamp, later);        // entry date floated to now
        QVERIFY(entries.first().editedAt.has_value());
        QCOMPARE(*entries.first().editedAt, later);        // edit date floated to now too
    }

    void memorySurfacesViaReadPlantJournal()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        const Plant basil = plantTracked(QStringLiteral("Basil"), QString(), kNowMs);
        plants.add(basil);
        FakeClock clock;
        clock.t = kNowMs;
        SetPlantMemoryTool setTool(plants, journal, clock);
        const QJsonObject args{{QStringLiteral("plant_id"), basil.id.toString()},
                               {QStringLiteral("text"), QStringLiteral("owner travels often")}};
        QVERIFY(!setTool.invoke(args).result().isError);

        ReadPlantJournalTool readTool(plants, journal);
        const QJsonObject readArgs{{QStringLiteral("plant_id"), basil.id.toString()}};
        const ToolOutcome o = readTool.invoke(readArgs).result();
        QVERIFY(!o.isError);
        QVERIFY(text(o).contains(QStringLiteral("Memory")));
        QVERIFY(text(o).contains(QStringLiteral("owner travels often")));
    }

    // --- set_global_memory / read_global_memory ---

    void globalMemorySpecRequiresTextOnly()
    {
        InMemoryJournalRepository journal;
        FakeClock clock;
        SetGlobalMemoryTool tool(journal, clock);

        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("set_global_memory"));
        const auto required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QVERIFY(required.contains(QStringLiteral("text")));
        // No plant_id — global memory is plant-less.
        QVERIFY(!spec.inputSchema.value(QStringLiteral("properties")).toObject()
                     .contains(QStringLiteral("plant_id")));
    }

    void globalMemoryMissingTextIsError()
    {
        InMemoryJournalRepository journal;
        FakeClock clock;
        clock.t = kNowMs;
        SetGlobalMemoryTool tool(journal, clock);

        const ToolOutcome o = tool.invoke(QJsonObject{}).result();
        QVERIFY(o.isError);
        QVERIFY(text(o).contains(QStringLiteral("text")));
        QVERIFY(journal.globalEntries().isEmpty()); // nothing written
    }

    void globalMemoryFirstCallCreatesOneGlobalEntry()
    {
        InMemoryJournalRepository journal;
        FakeClock clock;
        clock.t = kNowMs;
        SetGlobalMemoryTool tool(journal, clock);

        const QJsonObject args{{QStringLiteral("text"), QStringLiteral("hard tap water")}};
        QVERIFY(!tool.invoke(args).result().isError);

        const QList<JournalEntry> entries = journal.globalEntries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(!entries.first().plant.has_value()); // plant-less (global)
        QCOMPARE(entries.first().kind, JournalEntryKind::Memory);
        QCOMPARE(entries.first().note, QStringLiteral("hard tap water"));
        QCOMPARE(entries.first().timestamp, QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC));
        QVERIFY(!entries.first().editedAt.has_value()); // a creation, not an edit
    }

    void globalMemorySecondCallRewritesInPlaceFloatingBothDates()
    {
        InMemoryJournalRepository journal;
        FakeClock clock;
        clock.t = kNowMs;
        SetGlobalMemoryTool tool(journal, clock);

        QVERIFY(!tool.invoke(QJsonObject{{QStringLiteral("text"), QStringLiteral("hard water")}})
                     .result().isError);
        const JournalEntryId firstId = journal.globalEntries().first().id;

        const qint64 laterMs = kNowMs + 86400000;
        clock.t = laterMs;
        QVERIFY(!tool.invoke(QJsonObject{{QStringLiteral("text"),
                                          QStringLiteral("hard water; owner travels often")}})
                     .result().isError);

        const QList<JournalEntry> entries = journal.globalEntries();
        QCOMPARE(entries.size(), 1);                  // still exactly one global Memory entry
        QCOMPARE(entries.first().id, firstId);        // same stable id — an in-place rewrite
        QCOMPARE(entries.first().note, QStringLiteral("hard water; owner travels often"));
        const QDateTime later = QDateTime::fromMSecsSinceEpoch(laterMs, QTimeZone::UTC);
        QCOMPARE(entries.first().timestamp, later);   // entry date floated to now
        QVERIFY(entries.first().editedAt.has_value());
        QCOMPARE(*entries.first().editedAt, later);   // edit date floated to now too
    }

    void readGlobalMemoryEmptyThenSurfaces()
    {
        InMemoryJournalRepository journal;
        FakeClock clock;
        clock.t = kNowMs;

        ReadGlobalMemoryTool readTool(journal);
        const ToolOutcome empty = readTool.invoke(QJsonObject{}).result();
        QVERIFY(!empty.isError);
        QVERIFY(text(empty).contains(QStringLiteral("No global memory")));

        SetGlobalMemoryTool setTool(journal, clock);
        QVERIFY(!setTool.invoke(QJsonObject{{QStringLiteral("text"),
                                             QStringLiteral("dry climate")}}).result().isError);

        const ToolOutcome o = readTool.invoke(QJsonObject{}).result();
        QVERIFY(!o.isError);
        QVERIFY(text(o).contains(QStringLiteral("dry climate")));
    }

    // --- read_online_plant_db -------------------------------------------------------

    void webToolSpec()
    {
        FakeWebFetcher fetcher;
        ReadOnlinePlantDbTool tool(fetcher);
        const auto spec = tool.spec();
        QCOMPARE(spec.name, QStringLiteral("read_online_plant_db"));
        QVERIFY(!spec.description.isEmpty());
        const QJsonObject props = spec.inputSchema.value(QStringLiteral("properties")).toObject();
        QVERIFY(props.contains(QStringLiteral("query")));
        QVERIFY(props.contains(QStringLiteral("source")));
        const QJsonArray required = spec.inputSchema.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QCOMPARE(required.first().toString(), QStringLiteral("query"));
    }

    void webToolFetchesAndReducesToText()
    {
        FakeWebFetcher fetcher;
        fetcher.setResult(WebFetchResult{
            QByteArray("<html><body><p>Aloe vera is a <b>succulent</b>.</p></body></html>"), 200,
            std::nullopt, QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Aloe_vera"))});

        ReadOnlinePlantDbTool tool(fetcher);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("Aloe vera")}}).result();

        QVERIFY(!o.isError);
        // Default source is wikipedia; the host built the URL from the species (slug rule).
        QCOMPARE(fetcher.lastUrl(), QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Aloe_vera")));
        const QString t = text(o);
        QVERIFY(t.contains(QStringLiteral("From en.wikipedia.org"))); // provenance line
        QVERIFY(t.contains(QStringLiteral("Aloe vera is a succulent.")));
        QVERIFY(!t.contains(QLatin1Char('<'))); // HTML reduced away
    }

    void webToolHonoursSourceSelection()
    {
        FakeWebFetcher fetcher;
        fetcher.setResult(WebFetchResult{QByteArray("<p>Rosa</p>"), 200, std::nullopt,
                                         QUrl(QStringLiteral("https://species.wikimedia.org/wiki/Rosa"))});
        ReadOnlinePlantDbTool tool(fetcher);
        tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("Rosa")},
                                {QStringLiteral("source"), QStringLiteral("wikispecies")}})
            .result();
        QCOMPARE(fetcher.lastUrl(), QUrl(QStringLiteral("https://species.wikimedia.org/wiki/Rosa")));
    }

    void webToolTruncatesToBudget()
    {
        const QByteArray big = QByteArray("<p>") + QByteArray(webcontent::kTextBudget + 5000, 'x')
                               + QByteArray("</p>");
        FakeWebFetcher fetcher;
        fetcher.setResult(WebFetchResult{big, 200, std::nullopt,
                                         QUrl(QStringLiteral("https://en.wikipedia.org/wiki/X"))});
        ReadOnlinePlantDbTool tool(fetcher);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("X")}}).result();
        QVERIFY(!o.isError);
        QVERIFY(text(o).contains(QStringLiteral("[truncated]")));
    }

    void webToolBlankQueryIsError()
    {
        FakeWebFetcher fetcher;
        ReadOnlinePlantDbTool tool(fetcher);
        QVERIFY(tool.invoke(QJsonObject{}).result().isError);
        QVERIFY(tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("   ")}})
                    .result()
                    .isError);
        QCOMPARE(fetcher.callCount(), 0); // never reached the network
    }

    void webToolUnknownSourceIsError()
    {
        FakeWebFetcher fetcher;
        ReadOnlinePlantDbTool tool(fetcher);
        const ToolOutcome o = tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("Rosa")},
                                                      {QStringLiteral("source"),
                                                       QStringLiteral("hortipedia")}})
                                  .result();
        QVERIFY(o.isError);
        QCOMPARE(fetcher.callCount(), 0);
    }

    void webToolFetchErrorIsRecoverable()
    {
        FakeWebFetcher fetcher;
        fetcher.setResult(WebFetchResult{std::nullopt, 404,
                                         std::optional<QString>(QStringLiteral("the server returned HTTP 404")),
                                         QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Nope"))});
        ReadOnlinePlantDbTool tool(fetcher);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("Nope")}}).result();
        QVERIFY(o.isError); // surfaced as a recoverable isError outcome, not a turn failure
        QVERIFY(text(o).contains(QStringLiteral("404")));
    }

    void webToolEmptyBodyIsError()
    {
        FakeWebFetcher fetcher;
        fetcher.setResult(WebFetchResult{QByteArray(), 200, std::nullopt,
                                         QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Empty"))});
        ReadOnlinePlantDbTool tool(fetcher);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("query"), QStringLiteral("Empty")}}).result();
        QVERIFY(o.isError);
    }

    // --- read_plant_photo ---

    void photoUnknownPlantIsError()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryAttachmentRepository attachments;
        InMemoryAttachmentFileStore fileStore;
        ReadPlantPhotoTool tool(plants, journal, attachments, fileStore);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("plant_id"), QUuid::createUuidV7().toString(
                                                                     QUuid::WithoutBraces)}}).result();
        QVERIFY(o.isError);
    }

    void photoNoneIsBenignText()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryAttachmentRepository attachments;
        InMemoryAttachmentFileStore fileStore;
        const Plant p = plantTracked(QStringLiteral("Ficus"), QString(), kNowMs);
        plants.add(p);
        ReadPlantPhotoTool tool(plants, journal, attachments, fileStore);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("plant_id"), p.id.toString()}}).result();
        QVERIFY(!o.isError);
        QVERIFY(std::get<TextBlock>(o.parts.first()).text.contains(QStringLiteral("no journal photos")));
    }

    void photoReturnsImageParts()
    {
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryAttachmentRepository attachments;
        InMemoryAttachmentFileStore fileStore;
        const Plant p = plantTracked(QStringLiteral("Ficus"), QString(), kNowMs);
        plants.add(p);
        // Two journal entries, each with one photo; newest entry first, capped by `limit`.
        JournalEntry older{JournalEntryId::generate(), p.id,
                           QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC),
                           JournalEntryKind::Observation, QStringLiteral("old")};
        JournalEntry newer{JournalEntryId::generate(), p.id,
                           QDateTime::fromMSecsSinceEpoch(kNowMs + 1000, QTimeZone::UTC),
                           JournalEntryKind::Observation, QStringLiteral("new")};
        journal.add(older);
        journal.add(newer);
        Attachment ao{AttachmentId::generate(), older.id, QStringLiteral("attachments/o.jpg"),
                      QStringLiteral("old leaf"),
                      QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC)};
        Attachment an{AttachmentId::generate(), newer.id, QStringLiteral("attachments/n.png"),
                      QString(), QDateTime::fromMSecsSinceEpoch(kNowMs + 1000, QTimeZone::UTC)};
        attachments.add(ao);
        attachments.add(an);
        fileStore.put(ao.fileRef, QByteArray("oldbytes"));
        fileStore.put(an.fileRef, QByteArray("newbytes"));

        ReadPlantPhotoTool tool(plants, journal, attachments, fileStore);
        // limit 1 → only the newest entry's photo.
        const ToolOutcome o = tool.invoke(QJsonObject{
            {QStringLiteral("plant_id"), p.id.toString()}, {QStringLiteral("limit"), 1}}).result();
        QVERIFY(!o.isError);
        QCOMPARE(o.parts.size(), 2); // one text context line + one image
        QVERIFY(std::holds_alternative<TextBlock>(o.parts.at(0)));
        const auto &img = std::get<karness::ImageBlock>(o.parts.at(1));
        QCOMPARE(img.data, QByteArray("newbytes"));
        QCOMPARE(img.mimeType, QStringLiteral("image/png")); // from the .png extension
    }

    void photoSkipsMissingFile()
    {
        // A row whose file is absent (restored-without-files backup) is skipped, not an error.
        InMemoryPlantRepository plants;
        InMemoryJournalRepository journal;
        InMemoryAttachmentRepository attachments;
        InMemoryAttachmentFileStore fileStore;
        const Plant p = plantTracked(QStringLiteral("Ficus"), QString(), kNowMs);
        plants.add(p);
        JournalEntry e{JournalEntryId::generate(), p.id,
                       QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC),
                       JournalEntryKind::Observation, QString()};
        journal.add(e);
        // Row exists, but no bytes were put() into the store.
        attachments.add(Attachment{AttachmentId::generate(), e.id,
                                   QStringLiteral("attachments/gone.jpg"), QString(),
                                   QDateTime::fromMSecsSinceEpoch(kNowMs, QTimeZone::UTC)});
        ReadPlantPhotoTool tool(plants, journal, attachments, fileStore);
        const ToolOutcome o =
            tool.invoke(QJsonObject{{QStringLiteral("plant_id"), p.id.toString()}}).result();
        QVERIFY(!o.isError);
        QVERIFY(std::get<TextBlock>(o.parts.first()).text.contains(QStringLiteral("no journal photos")));
    }
};

QTEST_GUILESS_MAIN(TestAgentTools)
#include "test_agenttools.moc"
