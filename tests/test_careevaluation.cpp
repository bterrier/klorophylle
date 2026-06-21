// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "careevaluation.h"
#include "carestatus.h"
#include "catalogentry.h"
#include "csvcatalogrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "plant.h"

#include <array>

using namespace klr;

// The shared per-plant care orchestration (the binding/reading/range fetch + statusForReading
// dispatch) the plant-list pill and the alert evaluator both consume. Behaviour is the
// same the plant list already proves (test_plantlisthealth); these cases pin the snapshot
// shape the AlertController relies on: a status per current reading, and the worst-of rollup.
class TestCareEvaluation : public QObject {
    Q_OBJECT

    FakeClock m_clock;

    PlantId addPlant(InMemoryPlantRepository &plants, const QString &name, const QString &species = {})
    {
        Plant p;
        p.id = PlantId::generate();
        p.displayName = name;
        p.species = species;
        p.trackedSince = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        plants.add(p);
        return p.id;
    }

    static CareStatus statusFor(const PlantCareSnapshot &s, Quantity q)
    {
        for (int i = 0; i < s.current.size(); ++i)
            if (s.current.at(i).quantity == q)
                return s.statuses.at(i);
        return CareStatus::Unknown;
    }

private slots:
    void noRangesLeavesCurrentButUnknownLevel()
    {
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // none set, no species

        const PlantId plant = addPlant(plants, QStringLiteral("p"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 42.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });

        const PlantCareSnapshot snap =
            evaluatePlantCare(plants.all().first(), bindings, readings, thresholds, nullptr, now);
        QCOMPARE(snap.current.size(), 1);    // the value is still surfaced
        QVERIFY(snap.statuses.isEmpty());    // but nothing was judged
        QCOMPARE(snap.level, CareLevel::Unknown);
    }

    void perQuantityStatusesAndRollup()
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
        readings.append(s, std::array{
            Reading{ Quantity::SoilMoisture, 10.0, Unit::Percent, now, Provenance::Advertisement },
            Reading{ Quantity::AirHumidity, 55.0, Unit::Percent, now, Provenance::Advertisement } });
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 }); // 10 < 30
        thresholds.setRange(plant, CareRange{ Quantity::AirHumidity, 40.0, 70.0 });  // in range

        const PlantCareSnapshot snap =
            evaluatePlantCare(plants.all().first(), bindings, readings, thresholds, nullptr, now);
        QCOMPARE(statusFor(snap, Quantity::SoilMoisture), CareStatus::TooLow);
        QCOMPARE(statusFor(snap, Quantity::AirHumidity), CareStatus::Ideal);
        QCOMPARE(snap.level, CareLevel::Attention); // worst-of: the dry soil dominates
    }

    void judgesFromSpeciesCatalogIdeals()
    {
        // Mirrors the list-health contract: a speciesed plant judges against catalog ideals
        // with no per-plant override and no seeding step.
        InMemoryPlantRepository plants;
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds; // EMPTY

        CatalogEntry ficus;
        ficus.key = QStringLiteral("Ficus elastica");
        ficus.soilMoistureMin = 30.0;
        ficus.soilMoistureMax = 60.0;
        CsvCatalogRepository catalog({ ficus });

        const PlantId plant = addPlant(plants, QStringLiteral("Ficus"), QStringLiteral("Ficus elastica"));
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::SoilMoisture, 10.0, Unit::Percent, now,
                                                 Provenance::Advertisement } });

        const PlantCareSnapshot snap =
            evaluatePlantCare(plants.all().first(), bindings, readings, thresholds, &catalog, now);
        QCOMPARE(statusFor(snap, Quantity::SoilMoisture), CareStatus::TooLow);
        QCOMPARE(snap.level, CareLevel::Attention);
        QVERIFY(thresholds.thresholdsFor(plant).isEmpty()); // nothing materialised
    }
};

QTEST_GUILESS_MAIN(TestCareEvaluation)
#include "test_careevaluation.moc"
