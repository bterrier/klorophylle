// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "inmemorybindingrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "plant.h"
#include "reading.h"
#include "readingscsvexporter.h"

#include <QtCore/QTimeZone>

using namespace klr;

// Readings CSV export (ADR 0010): tidy/long layout, display-unit conversion, plant-facing
// attribution that follows the plant across a sensor swap, RFC-4180 quoting, absent->empty.
// Pure — seeded entirely from the in-memory repositories, no DB/QML/file IO.
class TestReadingsCsvExport : public QObject {
    Q_OBJECT

    FakeClock m_clock;
    const QDateTime kT0 { QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC };

    // A reading at a distinct hour so per-bucket collapse never merges our samples.
    static Reading soil(double v, const QDateTime &ts)
    {
        return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::History };
    }
    static Reading temp(double v, const QDateTime &ts)
    {
        return { Quantity::AirTemperature, v, Unit::DegreeCelsius, ts, Provenance::History };
    }

    // The data rows (everything after the header line), dropping the trailing blank.
    static QStringList dataRows(const QString &csv)
    {
        QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        lines.removeFirst(); // header
        return lines;
    }

private slots:
    void csvFieldQuotingRules()
    {
        QCOMPARE(csvField(QStringLiteral("plain")), QStringLiteral("plain"));
        QCOMPARE(csvField(QStringLiteral("a,b")), QStringLiteral("\"a,b\""));
        QCOMPARE(csvField(QStringLiteral("say \"hi\"")), QStringLiteral("\"say \"\"hi\"\"\""));
        QCOMPARE(csvField(QStringLiteral("line\nbreak")), QStringLiteral("\"line\nbreak\""));
    }

    void emptyDatasetIsHeaderOnly()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);
        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);

        const QString csv = exporter.exportCsv(DisplayUnits{}, kT0.addYears(-1), kT0.addYears(1));
        QCOMPARE(csv, QStringLiteral(
            "plant,species,sensor_model,timestamp,quantity,value,unit,provenance\n"));
    }

    void tidyRowsCarryPlantSensorAndUnit()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);

        Plant fern { PlantId::generate(), QStringLiteral("Fern"), QStringLiteral("Nephrolepis"), kT0 };
        plants.add(fern);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("FlowerCare"));
        bindings.bind(fern.id, s, kT0, std::nullopt);
        readings.append(s, std::array{ soil(40.0, kT0), soil(38.0, kT0.addSecs(3600)) });

        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);
        const QStringList rows = dataRows(exporter.exportCsv(DisplayUnits{}, kT0.addSecs(-1),
                                                             kT0.addDays(1)));
        QCOMPARE(rows.size(), 2);
        // plant,species,sensor_model,timestamp,quantity,value,unit,provenance
        const QStringList first = rows.first().split(QLatin1Char(','));
        QCOMPARE(first.at(0), QStringLiteral("Fern"));
        QCOMPARE(first.at(1), QStringLiteral("Nephrolepis"));
        QCOMPARE(first.at(2), QStringLiteral("FlowerCare"));
        QCOMPARE(first.at(4), QStringLiteral("SoilMoisture"));
        QCOMPARE(first.at(5), QStringLiteral("40"));
        QCOMPARE(first.at(6), QStringLiteral("%"));
        QCOMPARE(first.at(7), QStringLiteral("History"));
        // Timestamp round-trips to the original instant regardless of the test machine's TZ.
        QCOMPARE(QDateTime::fromString(first.at(3), Qt::ISODate).toUTC(), kT0);
    }

    void temperatureConvertsToDisplayUnit()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);

        Plant p { PlantId::generate(), QStringLiteral("Basil"), QString(), kT0 };
        plants.add(p);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("BB"), QStringLiteral("Thermo"));
        bindings.bind(p.id, s, kT0, std::nullopt);
        readings.append(s, std::array{ temp(20.0, kT0) });

        DisplayUnits f;
        f.temperature = TemperatureUnit::Fahrenheit;
        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);
        const QStringList row = dataRows(exporter.exportCsv(f, kT0.addSecs(-1), kT0.addDays(1)))
                                    .first().split(QLatin1Char(','));
        QCOMPARE(row.at(4), QStringLiteral("AirTemperature"));
        QCOMPARE(row.at(5), QStringLiteral("68")); // 20 °C -> 68 °F
        QCOMPARE(row.at(6), QStringLiteral("°F"));
    }

    void absentValueIsEmptyCell()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);

        Plant p { PlantId::generate(), QStringLiteral("Cactus"), QString(), kT0 };
        plants.add(p);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("CC"), QStringLiteral("FlowerCare"));
        bindings.bind(p.id, s, kT0, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, std::nullopt, Unit::Percent,
                                                 kT0, Provenance::History } });

        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);
        const QStringList row = dataRows(exporter.exportCsv(DisplayUnits{}, kT0.addSecs(-1),
                                                            kT0.addDays(1)))
                                    .first().split(QLatin1Char(','));
        QVERIFY(row.at(5).isEmpty()); // value cell empty, never -99
    }

    void commaInPlantNameIsQuoted()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);

        Plant p { PlantId::generate(), QStringLiteral("Ficus, the big one"), QString(), kT0 };
        plants.add(p);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("DD"), QStringLiteral("FlowerCare"));
        bindings.bind(p.id, s, kT0, std::nullopt);
        readings.append(s, std::array{ soil(50.0, kT0) });

        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);
        const QString line = dataRows(exporter.exportCsv(DisplayUnits{}, kT0.addSecs(-1),
                                                         kT0.addDays(1))).first();
        QVERIFY(line.startsWith(QStringLiteral("\"Ficus, the big one\",")));
    }

    void swapReHomesHistoryAndAttributesSensorPerSample()
    {
        InMemoryPlantRepository plants;
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemorySensorRepository sensors(m_clock);

        Plant orchid { PlantId::generate(), QStringLiteral("Orchid"), QString(), kT0 };
        plants.add(orchid);
        const SensorId a = sensors.ensure(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("OldProbe"));
        const SensorId b = sensors.ensure(HandleKind::Mac, QStringLiteral("B"), QStringLiteral("NewProbe"));
        const QDateTime swap = kT0.addDays(30);

        bindings.bind(orchid.id, a, kT0, std::nullopt);
        bindings.unbind(orchid.id, a, swap);
        bindings.bind(orchid.id, b, swap, std::nullopt);

        readings.append(a, std::array{ soil(55.0, kT0) });               // while A bound
        readings.append(a, std::array{ soil(99.0, kT0.addDays(40)) });   // after A unbound -> excluded
        readings.append(b, std::array{ soil(50.0, kT0.addDays(60)) });   // while B bound

        ReadingsCsvExporter exporter(plants, bindings, readings, sensors);
        const QStringList rows = dataRows(exporter.exportCsv(DisplayUnits{}, kT0.addSecs(-1),
                                                             kT0.addDays(90)));
        QCOMPARE(rows.size(), 2); // A's post-unbind sample is not the plant's history
        const QStringList r0 = rows.at(0).split(QLatin1Char(','));
        const QStringList r1 = rows.at(1).split(QLatin1Char(','));
        QCOMPARE(r0.at(2), QStringLiteral("OldProbe")); // sample at kT0 -> sensor A
        QCOMPARE(r0.at(5), QStringLiteral("55"));
        QCOMPARE(r1.at(2), QStringLiteral("NewProbe")); // sample at +60d -> sensor B
        QCOMPARE(r1.at(5), QStringLiteral("50"));
    }
};

QTEST_GUILESS_MAIN(TestReadingsCsvExport)
#include "test_readingscsvexport.moc"
