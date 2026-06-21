// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h"
#include "carethresholdsmodel.h"
#include "catalogentry.h"
#include "csvcatalogrepository.h"

#include <QtCore/QList>
#include "inmemorycarethresholdrepository.h"
#include "inmemorykeyvaluestore.h"
#include "inmemoryplantrepository.h"
#include "plant.h"
#include "settingsstore.h"

using namespace klr;

// The editable per-plant threshold list: edits round-trip through display-unit
// conversion, and resetToSpecies re-seeds from the catalog. No BLE/QML.
class TestCareThresholdsModel : public QObject {
    Q_OBJECT

    static int rowFor(const CareThresholdsModel &m, Quantity q)
    {
        for (int i = 0; i < m.rowCount(); ++i)
            if (m.data(m.index(i, 0), CareThresholdsModel::QuantityRole).toInt() == int(q))
                return i;
        return -1;
    }

    static CsvCatalogRepository catalogWith(const QString &key)
    {
        // One species, built directly (the CSV column layout is exercised in
        // test_catalogcsv): moisture 15..60 %, temperature 8..35 °C.
        CatalogEntry e;
        e.key = key;
        e.commonName = QStringLiteral("Rubber");
        e.soilMoistureMin = 15.0;
        e.soilMoistureMax = 60.0;
        e.temperatureMin = 8.0;
        e.temperatureMax = 35.0;
        return CsvCatalogRepository(QList<CatalogEntry>{ e });
    }

private slots:
    void editRoundTripsThroughDisplayUnit()
    {
        InMemoryCareThresholdRepository thresholds;
        CsvCatalogRepository catalog = catalogWith(QStringLiteral("Ficus"));
        InMemoryPlantRepository plants;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        CareThresholdsModel m(thresholds, catalog, plants, settings);

        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("F");
        plants.add(p);
        m.setPlant(p.id);

        // In °C, set air-temperature 10..30.
        m.setRange(int(Quantity::AirTemperature), QStringLiteral("10"), QStringLiteral("30"));
        const QList<CareRange> stored = thresholds.thresholdsFor(p.id);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().min, std::optional<double>(10.0)); // stored canonically (°C)

        // Switch to °F: the row now shows 50..86, and editing in °F stores back as °C.
        settings.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
        const int row = rowFor(m, Quantity::AirTemperature);
        QCOMPARE(m.data(m.index(row, 0), CareThresholdsModel::MinTextRole).toString(),
                 QStringLiteral("50"));
        m.setRange(int(Quantity::AirTemperature), QStringLiteral("68"), QStringLiteral("86"));
        QVERIFY(qAbs(*thresholds.thresholdsFor(p.id).first().min - 20.0) < 0.01); // 68°F -> 20°C
    }

    void blankOneSideAllowedBothSideRejected()
    {
        InMemoryCareThresholdRepository thresholds;
        CsvCatalogRepository catalog = catalogWith(QStringLiteral("Ficus"));
        InMemoryPlantRepository plants;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        CareThresholdsModel m(thresholds, catalog, plants, settings);

        Plant p;
        p.id = PlantId::generate();
        plants.add(p);
        m.setPlant(p.id);

        m.setRange(int(Quantity::SoilMoisture), QStringLiteral("20"), QString()); // only a min
        QList<CareRange> stored = thresholds.thresholdsFor(p.id);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().min, std::optional<double>(20.0));
        QVERIFY(!stored.first().max.has_value());

        // Blanking BOTH bounds is rejected — a threshold cannot be cleared away. The
        // previously stored range is left intact.
        m.setRange(int(Quantity::SoilMoisture), QString(), QString());
        stored = thresholds.thresholdsFor(p.id);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().min, std::optional<double>(20.0)); // unchanged
    }

    void showsSpeciesIdealsAndResetClearsOverrides()
    {
        InMemoryCareThresholdRepository thresholds;
        CsvCatalogRepository catalog = catalogWith(QStringLiteral("Ficus elastica"));
        InMemoryPlantRepository plants;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        CareThresholdsModel m(thresholds, catalog, plants, settings);

        Plant p;
        p.id = PlantId::generate();
        p.species = QStringLiteral("Ficus elastica");
        plants.add(p);
        m.setPlant(p.id);

        // Data-driven: with NO override, the editor already shows the species' catalog
        // ideals (temperature 8..35 °C) — nothing was seeded into the threshold table.
        const auto minOf = [&](Quantity q) {
            return m.data(m.index(rowFor(m, q), 0), CareThresholdsModel::MinTextRole).toString();
        };
        const auto maxOf = [&](Quantity q) {
            return m.data(m.index(rowFor(m, q), 0), CareThresholdsModel::MaxTextRole).toString();
        };
        QCOMPARE(minOf(Quantity::AirTemperature), QStringLiteral("8"));
        QCOMPARE(maxOf(Quantity::AirTemperature), QStringLiteral("35"));
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty()); // ideals are not materialised

        // Editing writes an OVERRIDE (only that quantity is stored).
        m.setRange(int(Quantity::AirTemperature), QStringLiteral("12"), QStringLiteral("30"));
        QCOMPARE(thresholds.thresholdsFor(p.id).size(), 1);
        QCOMPARE(minOf(Quantity::AirTemperature), QStringLiteral("12"));

        // "Reset to species" CLEARS the overrides; the editor falls back to the ideals.
        QSignalSpy spy(&m, &CareThresholdsModel::changed);
        m.resetToSpecies();
        QVERIFY(spy.count() >= 1);
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty());
        QCOMPARE(minOf(Quantity::AirTemperature), QStringLiteral("8"));
        QCOMPARE(maxOf(Quantity::AirTemperature), QStringLiteral("35"));
    }

    void dliRowShownInMmolWithoutUnitConversion()
    {
        InMemoryCareThresholdRepository thresholds;
        CsvCatalogRepository catalog = catalogWith(QStringLiteral("Ficus"));
        InMemoryPlantRepository plants;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        CareThresholdsModel m(thresholds, catalog, plants, settings);

        Plant p;
        p.id = PlantId::generate();
        plants.add(p);
        m.setPlant(p.id);

        // The light row is the Daily Light Integral dose, not the instantaneous lux:
        // there is a Dli row and NO Illuminance row in the editable pane.
        QVERIFY(rowFor(m, Quantity::Dli) >= 0);
        QCOMPARE(rowFor(m, Quantity::Illuminance), -1);
        const int row = rowFor(m, Quantity::Dli);
        QCOMPARE(m.data(m.index(row, 0), CareThresholdsModel::UnitRole).toString(),
                 QStringLiteral("mmol/m²/day"));

        // Set a dose range; it stores verbatim (no unit conversion) and round-trips.
        m.setRange(int(Quantity::Dli), QStringLiteral("3500"), QStringLiteral("30000"));
        const QList<CareRange> stored = thresholds.thresholdsFor(p.id);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().quantity, Quantity::Dli);
        QCOMPARE(stored.first().min, std::optional<double>(3500.0));
        QCOMPARE(stored.first().max, std::optional<double>(30000.0));

        // A µmol illuminance / °F preference must NOT rescale the DLI row (it has no
        // display-unit alternate — always mmol·m⁻²·day⁻¹).
        settings.setIlluminanceUnit(int(IlluminanceUnit::Micromole));
        settings.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
        QCOMPARE(m.data(m.index(row, 0), CareThresholdsModel::MinTextRole).toString(),
                 QStringLiteral("3500"));
        QCOMPARE(m.data(m.index(row, 0), CareThresholdsModel::UnitRole).toString(),
                 QStringLiteral("mmol/m²/day"));
    }

    void resetWithoutSpeciesIsNoOp()
    {
        InMemoryCareThresholdRepository thresholds;
        CsvCatalogRepository catalog = catalogWith(QStringLiteral("Ficus"));
        InMemoryPlantRepository plants;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        CareThresholdsModel m(thresholds, catalog, plants, settings);

        Plant p; // no species
        p.id = PlantId::generate();
        plants.add(p);
        m.setPlant(p.id);
        m.resetToSpecies();
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCareThresholdsModel)
#include "test_carethresholdsmodel.moc"
