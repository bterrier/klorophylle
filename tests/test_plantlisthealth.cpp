// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "carestatus.h"
#include "catalogentry.h"
#include "clock.h"
#include "csvcatalogrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "plant.h"
#include "plantlistmodel.h"

#include <array>

using namespace klr;

// The plant list's at-a-glance health rollup: given live readings + thresholds,
// each row carries a CareLevel. Without that context (the plant-only ctor) it is Unknown.
class TestPlantListHealth : public QObject {
    Q_OBJECT

    FakeClock m_clock; // epoch; bindings + readings + activeFor all align to it

    PlantId addPlant(InMemoryPlantRepository &plants, const QString &name)
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = name;
        p.trackedSince = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        plants.add(p);
        return p.id;
    }

    static int health(const PlantListModel &m, int row)
    {
        return m.data(m.index(row, 0), PlantListModel::HealthRole).toInt();
    }

    static QVariantMap moisture(const PlantListModel &m, int row)
    {
        return m.data(m.index(row, 0), PlantListModel::MoistureRole).toMap();
    }

    static int connectivity(const PlantListModel &m, int row)
    {
        return m.data(m.index(row, 0), PlantListModel::ConnectivityRole).toInt();
    }

private slots:
    // Connectivity (Liveness) is surfaced as its own role + name. The Offline/Stale/Live
    // transition matrix is owned by the pure livenessOf() (test_liveness); here we cover
    // the model wiring + guards: no dot (-1) without a bound sensor or without a scanner,
    // and refreshConnectivity() is a safe no-op in those cases.
    void connectivityRoleExists()
    {
        InMemoryPlantRepository plants;
        PlantListModel m(plants);
        const QHash<int, QByteArray> roles = m.roleNames();
        QCOMPARE(roles.value(PlantListModel::ConnectivityRole), QByteArray("connectivity"));
    }

    void connectivityIsMinusOneWithoutSensorOrScanner()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const PlantId plant = addPlant(plants, QStringLiteral("Fern"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);

        // A bound sensor, but no scanner injected -> we cannot judge broadcast freshness, so
        // the dot is suppressed (-1) rather than wrongly shown as red.
        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(connectivity(m, 0), -1);
        m.refreshConnectivity(); // no scanner -> no change, no crash
        QCOMPARE(connectivity(m, 0), -1);
    }

    void connectivityIsMinusOneForSensorlessPlant()
    {
        InMemoryPlantRepository plants;
        addPlant(plants, QStringLiteral("sensorless"));
        PlantListModel m(plants);
        QCOMPARE(connectivity(m, 0), -1); // no sensor bound -> no dot
    }

    void unknownWithoutHealthContext()
    {
        InMemoryPlantRepository plants;
        addPlant(plants, QStringLiteral("p"));
        PlantListModel m(plants); // plant-only ctor: no health context
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(health(m, 0), int(CareLevel::Unknown));
    }

    void rollsUpReadingsAgainstThresholds()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const PlantId plant = addPlant(plants, QStringLiteral("Fern"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 10.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 }); // 10 < 30

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(health(m, 0), int(CareLevel::Attention)); // soil too low

        // Raise the reading into range -> Good after refresh.
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 45.0, Unit::Percent,
                                                 now.addSecs(60), Provenance::Advertisement } });
        m.refresh();
        QCOMPARE(health(m, 0), int(CareLevel::Good));
    }

    // Light dose (+ ADR 0015 min-only): the list-health rollup judges light on its Daily Light
    // Integral (dose) against the species' Dli range — NOT a daily peak — and MIN-ONLY: a
    // dose above the catalog max is "ample" (Good), only a dose below the min (too little
    // daily light) reads Attention. A plant getting a full bright day is fine; a genuinely
    // dim plant is flagged. (No over-flagging of sun-exposed plants — the whole point of the dose rule.)
    void rollsUpLightOnDailyIntegral()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDate d(2026, 1, 5);                       // a completed local day to judge
        m_clock.t = QDateTime(d, QTime(5, 0)).toMSecsSinceEpoch(); // bind before the day's data
        const QDateTime bindAt = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        auto lux = [](double v, const QTime &t) {
            return Reading{ Quantity::Illuminance, v, Unit::Lux, QDateTime(QDate(2026, 1, 5), t),
                            Provenance::Advertisement };
        };

        const PlantId hot = addPlant(plants, QStringLiteral("Sun lover"));
        const SensorId sa = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(hot, sa, bindAt, std::nullopt);
        readings.append(sa, std::array{ lux(80000.0, QTime(6, 0)), lux(80000.0, QTime(18, 0)) });
        thresholds.setRange(hot, CareRange{ Quantity::Dli, 3500.0, 30000.0 });

        const PlantId dim = addPlant(plants, QStringLiteral("Dim corner"));
        const SensorId sb = sensors.ensure(HandleKind::Mac, QStringLiteral("BB"), QStringLiteral("m"));
        bindings.bind(dim, sb, bindAt, std::nullopt);
        readings.append(sb, std::array{ lux(800.0, QTime(6, 0)), lux(800.0, QTime(18, 0)) });
        // ~800 lux all day → ~640 mmol, well under the 3500 min → not enough daily light.
        thresholds.setRange(dim, CareRange{ Quantity::Dli, 3500.0, 30000.0 });

        m_clock.t = QDateTime(QDate(2026, 1, 6), QTime(12, 0)).toMSecsSinceEpoch(); // next day
        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(health(m, 0), int(CareLevel::Good));      // sun lover: ample light, min-only → fine
        QCOMPARE(health(m, 1), int(CareLevel::Attention)); // dim corner: dose below the min
    }

    // Each row also carries the current moisture/light as a card metric —
    // present + formatted value + the value's 0..1 position in the plant's ideal range.
    void surfacesMoistureMetricWithIdealRange()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const PlantId plant = addPlant(plants, QStringLiteral("Fern"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 55.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 80.0 });

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        const QVariantMap moist = moisture(m, 0);
        QCOMPARE(moist.value("present").toBool(), true);
        QCOMPARE(moist.value("hasRange").toBool(), true);
        QCOMPARE(moist.value("fraction").toDouble(), 0.5); // (55-30)/(80-30)
        QVERIFY(!moist.value("valueText").toString().isEmpty());
    }

    void metricClampsToRangeEnds()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const PlantId dry = addPlant(plants, QStringLiteral("Dry"));
        const PlantId wet = addPlant(plants, QStringLiteral("Wet"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId sd = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        const SensorId sw = sensors.ensure(HandleKind::Mac, QStringLiteral("BB"), QStringLiteral("m"));
        bindings.bind(dry, sd, now, std::nullopt);
        bindings.bind(wet, sw, now, std::nullopt);
        readings.append(sd, std::array{ Reading{ Quantity::SoilMoisture, 10.0, Unit::Percent, now,
                                                  Provenance::Advertisement } }); // below min
        readings.append(sw, std::array{ Reading{ Quantity::SoilMoisture, 99.0, Unit::Percent, now,
                                                  Provenance::Advertisement } }); // above max
        thresholds.setRange(dry, CareRange{ Quantity::SoilMoisture, 30.0, 80.0 });
        thresholds.setRange(wet, CareRange{ Quantity::SoilMoisture, 30.0, 80.0 });

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(moisture(m, 0).value("fraction").toDouble(), 0.0); // clamped low
        QCOMPARE(moisture(m, 1).value("fraction").toDouble(), 1.0); // clamped high
    }

    void metricPresentButNoBarWithoutRange()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // no threshold for moisture

        const PlantId plant = addPlant(plants, QStringLiteral("p"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 42.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        const QVariantMap moist = moisture(m, 0);
        QCOMPARE(moist.value("present").toBool(), true);  // value shown as a chip
        QCOMPARE(moist.value("hasRange").toBool(), false); // but no bar
        QVERIFY(!moist.value("valueText").toString().isEmpty());
    }

    void metricAbsentWithoutReading()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        addPlant(plants, QStringLiteral("sensorless")); // no binding, no reading
        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(moisture(m, 0).value("present").toBool(), false);
    }

    void judgesFromSpeciesCatalogIdealsWithoutOverrides()
    {
        // The data-driven contract: a plant with a SPECIES is judged against the catalog's
        // ideal ranges with NO per-plant override and NO seeding step (ADR 0003).
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // deliberately EMPTY (no overrides)

        CatalogEntry ficus;
        ficus.key = QStringLiteral("Ficus elastica");
        ficus.soilMoistureMin = 30.0;
        ficus.soilMoistureMax = 60.0;
        CsvCatalogRepository catalog({ ficus });

        Plant p;
        p.id = PlantId::generate();
        p.displayName = QStringLiteral("Ficus");
        p.species = QStringLiteral("Ficus elastica");
        p.trackedSince = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        plants.add(p);

        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(p.id, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 10.0, Unit::Percent, now,
                                                 Provenance::Advertisement } }); // 10 < 30

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, &catalog, &m_clock);
        QCOMPARE(health(m, 0), int(CareLevel::Attention)); // judged from catalog ideals alone
        QVERIFY(thresholds.thresholdsFor(p.id).isEmpty());  // nothing was materialised
    }

    void unknownWhenNoThresholdSet()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // none set

        const PlantId plant = addPlant(plants, QStringLiteral("p"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 45.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });

        PlantListModel m(plants, &sensors, &bindings, &readings, &thresholds, nullptr, &m_clock);
        QCOMPARE(health(m, 0), int(CareLevel::Unknown)); // nothing to judge against
    }
};

QTEST_GUILESS_MAIN(TestPlantListHealth)
#include "test_plantlisthealth.moc"
