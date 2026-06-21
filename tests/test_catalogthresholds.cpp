// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h"
#include "catalogthresholds.h"

using namespace klr;

// The pure catalog -> ideal-range seed: the measured quantities map in the
// catalog's canonical unit, plus the DLI mmol column as Quantity::Dli. pH is not.
class TestCatalogThresholds : public QObject {
    Q_OBJECT

private slots:
    void mapsTheSupportedQuantities()
    {
        CatalogEntry e;
        e.key = QStringLiteral("Ficus elastica");
        e.soilMoistureMin = 15.0;
        e.soilMoistureMax = 60.0;
        e.soilConductivityMin = 350.0;
        e.soilConductivityMax = 2000.0;
        e.temperatureMin = 8.0;
        e.temperatureMax = 35.0;
        e.humidityMin = 30.0;
        e.humidityMax = 80.0;
        e.lightLuxMin = 1500.0;
        e.lightLuxMax = 60000.0;
        e.lightMmolMin = 3500.0; // DLI column → Quantity::Dli
        e.lightMmolMax = 30000.0;
        // Not mapped:
        e.soilPhMin = 5.0;

        const QList<CareRange> ranges = idealRanges(e);
        QCOMPARE(ranges.size(), 6); // 5 measured quantities + the DLI dose range

        const std::span<const CareRange> span(ranges.constData(), ranges.size());
        const auto temp = rangeFor(span, Quantity::AirTemperature);
        QVERIFY(temp.has_value());
        QCOMPARE(temp->min, std::optional<double>(8.0));
        QCOMPARE(temp->max, std::optional<double>(35.0));

        // The lux range survives (for the display bar) AND the mmol column maps to Dli.
        const auto lux = rangeFor(span, Quantity::Illuminance);
        QVERIFY(lux.has_value());
        QCOMPARE(lux->max, std::optional<double>(60000.0));
        const auto dli = rangeFor(span, Quantity::Dli);
        QVERIFY(dli.has_value());
        QCOMPARE(dli->min, std::optional<double>(3500.0));
        QCOMPARE(dli->max, std::optional<double>(30000.0));

        // pH has no Quantity; the DLI column is Quantity::Dli, never the instantaneous Ppfd.
        QVERIFY(!rangeFor(span, Quantity::Ppfd).has_value());
    }

    void skipsAllBlankFields()
    {
        CatalogEntry e; // every range nullopt
        QVERIFY(idealRanges(e).isEmpty());
    }

    void oneSidedBoundStillMapped()
    {
        CatalogEntry e;
        e.soilMoistureMin = 20.0; // only the lower bound present
        const QList<CareRange> ranges = idealRanges(e);
        QCOMPARE(ranges.size(), 1);
        QCOMPARE(ranges.first().quantity, Quantity::SoilMoisture);
        QCOMPARE(ranges.first().min, std::optional<double>(20.0));
        QVERIFY(!ranges.first().max.has_value());
    }

    // The data-driven resolver: per-quantity, an override wins over the ideal; quantities
    // present in only one source are kept (the union).
    void mergeRangesOverrideWinsPerQuantity()
    {
        const QList<CareRange> ideals{
            { Quantity::SoilMoisture, 15.0, 60.0 },
            { Quantity::AirTemperature, 8.0, 35.0 },
        };
        const QList<CareRange> overrides{
            { Quantity::SoilMoisture, 40.0, 50.0 },     // overrides the ideal
            { Quantity::AirHumidity, 30.0, std::nullopt }, // only in overrides
        };
        const QList<CareRange> merged = mergeRanges(ideals, overrides);
        QCOMPARE(merged.size(), 3); // moisture, temperature, humidity

        const std::span<const CareRange> s(merged.constData(), merged.size());
        QCOMPARE(rangeFor(s, Quantity::SoilMoisture)->min, std::optional<double>(40.0)); // override
        QCOMPARE(rangeFor(s, Quantity::AirTemperature)->max, std::optional<double>(35.0)); // ideal kept
        QVERIFY(rangeFor(s, Quantity::AirHumidity).has_value()); // override-only quantity kept

        // No overrides -> just the ideals; no ideals -> just the overrides.
        QCOMPARE(mergeRanges(ideals, {}).size(), 2);
        QCOMPARE(mergeRanges({}, overrides).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestCatalogThresholds)
#include "test_catalogthresholds.moc"
