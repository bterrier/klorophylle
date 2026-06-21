// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "ids.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorykeyvaluestore.h"
#include "inmemorysensorrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "plantcaremodel.h"
#include "reading.h"
#include "settingsstore.h"

#include <array>
#include <span>

using namespace klr;

// The per-plant care aggregate, exercised with no BLE/QML: attach/detach/ingest +
// the current-readings list model. Binding windows and activeFor() share the SAME
// injected clock, so the model resolves deterministically (ADR 0005).
class TestPlantCareModel : public QObject {
    Q_OBJECT

    FakeClock m_clock; // t == 0 (epoch); attach/activeFor both read it
    InMemoryKeyValueStore m_kv;
    SettingsStore m_settings { &m_kv }; // default (canonical) units unless a test changes them

    static Reading soil(double v, const QDateTime &ts)
    {
        return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::Advertisement };
    }

    static Reading battery(double pct, const QDateTime &ts)
    {
        return { Quantity::Battery, pct, Unit::Percent, ts, Provenance::Advertisement };
    }

    static int rowForQuantity(const PlantCareModel &m, Quantity q)
    {
        for (int i = 0; i < m.rowCount(); ++i)
            if (m.data(m.index(i, 0), PlantCareModel::QuantityRole).toInt() == int(q))
                return i;
        return -1;
    }

    // Feed a reading as if it arrived from a bound sensor's live broadcast. Ingestion now lives in
    // the always-on ReadingIngester (see test_readingingester); PlantCareModel is a pure READER, so
    // a test just lands the rows in the sensor-keyed repo (resolving the handle attach() minted)
    // and recomputes — exactly what AppContext does after an admit.
    static void feed(ISensorRepository &sensors, IReadingRepository &readings, PlantCareModel &care,
                     const QString &handle, std::span<const Reading> rs)
    {
        const std::optional<Sensor> s = sensors.findByHandle(HandleKind::Mac, handle);
        if (s)
            readings.append(s->id, rs);
        care.refresh();
    }

private slots:
    void attachShowsAggregatedCurrentReadings()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);

        care.setPlant(PlantId::generate());
        QCOMPARE(care.rowCount(), 0);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        const Reading snap[] = { soil(40.0, t0) };
        care.attach(HandleKind::Mac, QStringLiteral("AA:BB"), QStringLiteral("Flower care"), snap);

        QCOMPARE(care.rowCount(), 1);
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QVERIFY(row >= 0);
        QVERIFY(care.data(care.index(row, 0), PlantCareModel::ValueTextRole).toString().contains(
            QStringLiteral("40")));

        const QVariantList bound = care.boundSensors();
        QCOMPARE(bound.size(), 1);
        QCOMPARE(bound.first().toMap().value(QStringLiteral("model")).toString(),
                 QStringLiteral("Flower care"));
    }

    void batteryIsNotACareReading()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);

        const PlantId plant = PlantId::generate();
        // A real threshold so an in-range soil reading rolls up to a definite verdict.
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 });
        care.setPlant(plant);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        // A sensor that reports BOTH soil moisture and battery.
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(45.0, t0), battery(80.0, t0) });

        // Battery is a sensor property, not a plant-care reading — no row for it.
        QCOMPARE(rowForQuantity(care, Quantity::Battery), -1);
        QVERIFY(rowForQuantity(care, Quantity::SoilMoisture) >= 0);

        // And it must not affect the health rollup: the soil reading alone makes it Ideal.
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
    }

    void multipleSensorsCollapseNewestWins()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(PlantId::generate());

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(40.0, t0) });
        care.attach(HandleKind::Mac, QStringLiteral("B"), QStringLiteral("b"),
                    std::array{ soil(60.0, t0.addSecs(3600)) }); // fresher

        QCOMPARE(care.rowCount(), 1); // one quantity, two sensors collapsed
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QVERIFY(care.data(care.index(row, 0), PlantCareModel::ValueTextRole).toString().contains(
            QStringLiteral("60")));
        QCOMPARE(care.boundSensors().size(), 2);
    }

    void detachClosesBinding()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(PlantId::generate());

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(40.0, t0) });
        const QString sensorId =
            care.boundSensors().first().toMap().value(QStringLiteral("sensorId")).toString();

        care.detach(SensorId{ QUuid::fromString(sensorId) });
        QVERIFY(care.boundSensors().isEmpty());
        QCOMPARE(care.rowCount(), 0); // no active sensor -> no current values
    }

    void setPlantNulloptClears()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(PlantId::generate());
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(40.0, QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC)) });
        QCOMPARE(care.rowCount(), 1);

        care.setPlant(std::nullopt);
        QCOMPARE(care.rowCount(), 0);
        QVERIFY(care.boundSensors().isEmpty());
        QVERIFY(!care.hasPlant());
    }

    void unitPreferenceConvertsCurrentReadings()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, settings);

        care.setPlant(PlantId::generate());
        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ Reading{ Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, t0,
                                         Provenance::Advertisement } });

        const int row = rowForQuantity(care, Quantity::AirTemperature);
        QVERIFY(row >= 0);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::ValueTextRole).toString(),
                 QStringLiteral("20.0 °C"));

        // Switching to Fahrenheit re-formats the stored °C value with no DB change
        // (the care model refreshes on a unit change, emitting modelReset).
        QSignalSpy spy(&care, &QAbstractItemModel::modelReset);
        settings.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
        QVERIFY(spy.count() >= 1);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::ValueTextRole).toString(),
                 QStringLiteral("68.0 °F"));
    }

    void statusReflectsThresholds()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 });
        care.setPlant(plant);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(10.0, t0) }); // below the 30% minimum

        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QVERIFY(row >= 0);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::TooLow));

        // A fresh in-range sample flips the status to Ideal (status judges the canonical
        // value, independent of the unit preference).
        feed(sensors, readings, care, QStringLiteral("A"), std::array{ soil(45.0, t0.addSecs(3600)) });
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
    }

    // A day's worth of illuminance samples on local date `d` (timestamps in LOCAL time so
    // they line up with the local-midnight day boundary meanDailyLightIntegral uses).
    static Reading lux(double v, const QDateTime &ts)
    {
        return { Quantity::Illuminance, v, Unit::Lux, ts, Provenance::Advertisement };
    }

    void lightModerateDayWithSunSpikeIsIdeal()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDate d(2026, 1, 5);                     // a completed local day to judge
        m_clock.t = QDateTime(d, QTime(5, 0)).toMSecsSinceEpoch(); // bind before the day's data

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::Dli, 3500.0, 30000.0 }); // moderate species
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        // A mostly-dim day with ~1 h of direct midday sun (~120k lux). The integrated dose
        // is ~8800 mmol — inside 3500–30000 — so it reads Ideal. This is the exact case the
        // old 3-day-peak test flagged TooHigh (the over-flagging the dose rule eliminates).
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ lux(0.0, QDateTime(d, QTime(6, 0))),
                                lux(2000.0, QDateTime(d, QTime(11, 0))),
                                lux(120000.0, QDateTime(d, QTime(12, 0))), // 1 h direct sun
                                lux(2000.0, QDateTime(d, QTime(13, 0))),
                                lux(0.0, QDateTime(d, QTime(18, 0))) });

        m_clock.t = QDateTime(QDate(2026, 1, 6), QTime(12, 0)).toMSecsSinceEpoch(); // next day
        care.refresh();

        // The verdict is carried by the synthesized "Daily light" (DLI) row…
        const int dli = rowForQuantity(care, Quantity::Dli);
        QVERIFY(dli >= 0);
        QCOMPARE(care.data(care.index(dli, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
        QVERIFY(care.data(care.index(dli, 0), PlantCareModel::ValueTextRole).toString().contains(
            QStringLiteral("mmol")));
        // …while the instantaneous light row stays for reference, but is not itself judged.
        const int lightRow = rowForQuantity(care, Quantity::Illuminance);
        QVERIFY(lightRow >= 0);
        QCOMPARE(care.data(care.index(lightRow, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Unknown));
    }

    void lightSustainedHighDoseIsIdeal() // min-only: ample daily light is never flagged
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDate d(2026, 1, 5);
        m_clock.t = QDateTime(d, QTime(5, 0)).toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::Dli, 3500.0, 30000.0 });
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        // 80000 lux sustained across the daytime → ~64000 mmol, well over the 30000 catalog
        // max — but that max is the top of an *ideal* band, not a tolerance ceiling, so a dose
        // above it reads Ideal ("ample"), NOT TooHigh. Light is judged min-only (see ADR 0015).
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ lux(80000.0, QDateTime(d, QTime(6, 0))),
                                lux(80000.0, QDateTime(d, QTime(18, 0))) });

        m_clock.t = QDateTime(QDate(2026, 1, 6), QTime(12, 0)).toMSecsSinceEpoch();
        care.refresh();

        const int dli = rowForQuantity(care, Quantity::Dli);
        QVERIFY(dli >= 0);
        QCOMPARE(care.data(care.index(dli, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
    }

    void lightDimDayIsTooLow()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDate d(2026, 1, 5);
        m_clock.t = QDateTime(d, QTime(5, 0)).toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::Dli, 3500.0, 30000.0 });
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        // A dim corner: ~500 lux all day → ~400 mmol, far below the 3500 floor → TooLow.
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ lux(500.0, QDateTime(d, QTime(6, 0))),
                                lux(500.0, QDateTime(d, QTime(18, 0))) });

        m_clock.t = QDateTime(QDate(2026, 1, 6), QTime(12, 0)).toMSecsSinceEpoch();
        care.refresh();

        const int dli = rowForQuantity(care, Quantity::Dli);
        QVERIFY(dli >= 0);
        QCOMPARE(care.data(care.index(dli, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::TooLow));
    }

    void lightVerdictWithheldUntilAFullDay()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDateTime today(QDate(2026, 1, 5), QTime(12, 0)); // local, still in progress
        m_clock.t = QDateTime(QDate(2026, 1, 5), QTime(5, 0)).toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::Dli, 3500.0, 30000.0 });
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        // A sensor paired this morning has data only for the in-progress day — no COMPLETED
        // day yet, so the dose can't be judged: Unknown, not a false TooLow. (The dose rule's
        // partial-day guard, the analogue of the old fresh-sensor grace.)
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ lux(2.0, QDateTime(QDate(2026, 1, 5), QTime(7, 0))),
                                lux(800.0, QDateTime(QDate(2026, 1, 5), QTime(11, 0))) });
        m_clock.t = today.toMSecsSinceEpoch();
        care.refresh();

        // The "Daily light" row appears but has no dose yet — value "—", status Unknown.
        const int dli = rowForQuantity(care, Quantity::Dli);
        QVERIFY(dli >= 0);
        QCOMPARE(care.data(care.index(dli, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Unknown));
        QVERIFY(!care.data(care.index(dli, 0), PlantCareModel::PresentRole).toBool());
    }

    void singleInRangeTemperatureIsIdeal()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDateTime nowT(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
        m_clock.t = nowT.toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::AirTemperature, 10.0, 30.0 });
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        // ONE reading, timestamped a minute BEFORE the binding (as a real advertisement
        // snapshot is) — so it falls outside the binding-clipped history window. The
        // current value must still be judged: 20 °C is in range ⇒ Ideal, not Unknown.
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ Reading{ Quantity::AirTemperature, 20.0, Unit::DegreeCelsius,
                                         nowT.addSecs(-60), Provenance::Advertisement } });
        const int row = rowForQuantity(care, Quantity::AirTemperature);
        QVERIFY(row >= 0);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
    }

    void temperatureExcursionRememberedThenClears()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;

        const QDateTime night(QDate(2026, 1, 1), QTime(3, 0), QTimeZone::UTC);
        m_clock.t = night.toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::AirTemperature, 10.0, 30.0 });
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(plant);

        auto temp = [](double v, const QDateTime &ts) {
            return Reading{ Quantity::AirTemperature, v, Unit::DegreeCelsius, ts,
                            Provenance::Advertisement };
        };

        // 03:00 — a 4 °C cold snap (below the 10 °C minimum).
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ temp(4.0, night) });
        const int row = rowForQuantity(care, Quantity::AirTemperature);
        QVERIFY(row >= 0);

        // 11:00 — recovered to a comfortable 20 °C. The CURRENT value is fine now, but the
        // morning check still flags the night's cold snap (within the 24h window).
        m_clock.t = night.addSecs(8 * 3600).toMSecsSinceEpoch();
        feed(sensors, readings, care, QStringLiteral("A"), std::array{ temp(20.0, night.addSecs(8 * 3600)) });
        QVERIFY(care.data(care.index(row, 0), PlantCareModel::ValueTextRole).toString().contains(
            QStringLiteral("20")));
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::TooLow));

        // 28h after the snap, with only warm readings left in the window, it clears.
        m_clock.t = night.addSecs(28 * 3600).toMSecsSinceEpoch();
        feed(sensors, readings, care, QStringLiteral("A"), std::array{ temp(21.0, night.addSecs(28 * 3600)) });
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Ideal));
    }

    void fractionReflectsPositionInRange()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::SoilMoisture, 30.0, 60.0 });
        care.setPlant(plant);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(45.0, t0) }); // mid-range → (45-30)/(60-30) = 0.5
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QVERIFY(row >= 0);
        QVERIFY(care.data(care.index(row, 0), PlantCareModel::HasRangeRole).toBool());
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::FractionRole).toDouble(), 0.5);

        // Above the max clamps to 1.0 (bar fills, never overflows).
        feed(sensors, readings, care, QStringLiteral("A"), std::array{ soil(90.0, t0.addSecs(3600)) });
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::FractionRole).toDouble(), 1.0);
    }

    void fractionAbsentWithoutBothBounds()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(PlantId::generate()); // no thresholds → no range to position against

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(45.0, t0) });
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QVERIFY(row >= 0);
        QVERIFY(!care.data(care.index(row, 0), PlantCareModel::HasRangeRole).toBool());
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::FractionRole).toDouble(), 0.0);
    }

    void statusUnknownWithoutThreshold()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, m_settings);
        care.setPlant(PlantId::generate()); // no thresholds seeded

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ soil(45.0, t0) });
        const int row = rowForQuantity(care, Quantity::SoilMoisture);
        QCOMPARE(care.data(care.index(row, 0), PlantCareModel::StatusRole).toInt(),
                 int(CareStatus::Unknown));
    }

    void historyBandFollowsThresholdAndUnit()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        InMemoryCareThresholdRepository thresholds;
        InMemoryKeyValueStore kv;
        SettingsStore settings(&kv);
        PlantCareModel care(sensors, bindings, readings, thresholds, m_clock, settings);

        const QDateTime t0(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        // Align the clock with the sample so the binding window (validFrom == now) and
        // the history range [now-10y, now] both include it.
        m_clock.t = t0.toMSecsSinceEpoch();

        const PlantId plant = PlantId::generate();
        thresholds.setRange(plant, CareRange{ Quantity::AirTemperature, 10.0, 30.0 }); // °C
        care.setPlant(plant);

        care.attach(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"),
                    std::array{ Reading{ Quantity::AirTemperature, 20.0, Unit::DegreeCelsius, t0,
                                         Provenance::Advertisement } });

        care.loadHistory(Quantity::AirTemperature);
        QVERIFY(care.history()->hasBand());
        // °C band [10,30] shown in °C by default; switching to °F re-converts it (50, 86).
        QCOMPARE(care.history()->bandMin(), 10.0);
        settings.setTemperatureUnit(int(TemperatureUnit::Fahrenheit));
        QVERIFY(care.history()->hasBand());
        QVERIFY(qAbs(care.history()->bandMin() - 50.0) < 0.01);
        QVERIFY(qAbs(care.history()->bandMax() - 86.0) < 0.01);
    }
};

QTEST_GUILESS_MAIN(TestPlantCareModel)
#include "test_plantcaremodel.moc"
