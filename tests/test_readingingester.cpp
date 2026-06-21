// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "binding.h"
#include "clock.h"
#include "ids.h"
#include "inmemorybindingrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "reading.h"
#include "readingingester.h"

#include <array>

using namespace klr;

// The always-on, sensor-level ingestion seam (ADR 0011): a registered sensor's broadcasts are
// persisted the whole time the app runs — NO plant/screen needed — and the owned WriteCadenceGate
// dedups per (SensorId, Quantity) across the app's life. Exercised with the in-memory repo fakes.
class TestReadingIngester : public QObject {
    Q_OBJECT

    FakeClock m_clock; // t == 0 (epoch); used only as the fallback for an undated reading

    static Reading soil(double v, const QDateTime &ts)
    {
        return { Quantity::SoilMoisture, v, Unit::Percent, ts, Provenance::Advertisement };
    }
    static QDateTime t0() { return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC); }

private slots:
    // An unregistered handle has no Sensor row -> nothing stored, nullopt returned.
    void unregisteredHandleDropped()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryReadingRepository readings;
        ReadingIngester ingester(sensors, readings, m_clock);

        const auto wrote =
            ingester.ingest(HandleKind::Mac, QStringLiteral("AA:BB"), std::array{ soil(40.0, t0()) });
        QCOMPARE(wrote, std::nullopt);

        // And it really wrote nothing: registering the same handle afterwards finds no readings.
        const SensorId id = sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB"), QStringLiteral("Flower care"));
        QVERIFY(!readings.latest(id, Quantity::SoilMoisture).has_value());
    }

    // The core regression: a registered sensor's reading is stored with NO plant open.
    void registeredSensorStoresWithoutPlant()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryReadingRepository readings;
        ReadingIngester ingester(sensors, readings, m_clock);

        const SensorId id = sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB"), QStringLiteral("Flower care"));
        const auto wrote =
            ingester.ingest(HandleKind::Mac, QStringLiteral("AA:BB"), std::array{ soil(40.0, t0()) });
        QCOMPARE(wrote, id);

        const auto latest = readings.latest(id, Quantity::SoilMoisture);
        QVERIFY(latest.has_value());
        QCOMPARE(latest->value.value(), 40.0);
    }

    // Gate parity (mirrors test_bucket): same value in one bucket -> one write; a changed value
    // OR a new bucket -> re-admit. Across the SAME ingester instance.
    void gateCollapsesRepeatsAdmitsChangeAndNewBucket()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryReadingRepository readings;
        ReadingIngester ingester(sensors, readings, m_clock);
        sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB"), QStringLiteral("Flower care"));

        const QString h = QStringLiteral("AA:BB");
        const QDateTime t = t0();
        QVERIFY(ingester.ingest(HandleKind::Mac, h, std::array{ soil(40.0, t) }).has_value());
        // Same value, same bucket -> redundant repeat, no write.
        QCOMPARE(ingester.ingest(HandleKind::Mac, h, std::array{ soil(40.0, t.addSecs(5)) }), std::nullopt);
        // Changed value, same bucket -> re-admit.
        QVERIFY(ingester.ingest(HandleKind::Mac, h, std::array{ soil(41.0, t.addSecs(10)) }).has_value());
        // Same value again but a NEW hour bucket -> re-admit (a bucket is never skipped).
        QVERIFY(ingester.ingest(HandleKind::Mac, h, std::array{ soil(41.0, t.addSecs(3600)) }).has_value());
    }

    // A sensor bound to two plants: stored ONCE, yet BOTH plants resolve it via currentForPlant
    // (the reading repo fans out across bindings; the ingester is plant-agnostic).
    void sharedSensorStoredOnceSeenByBothPlants()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        ReadingIngester ingester(sensors, readings, m_clock);

        const SensorId id = sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB"), QStringLiteral("Flower care"));
        const PlantId p1 = PlantId::generate();
        const PlantId p2 = PlantId::generate();
        const QDateTime when = t0();
        bindings.bind(p1, id, when, std::nullopt);
        bindings.bind(p2, id, when, std::nullopt);

        QVERIFY(ingester.ingest(HandleKind::Mac, QStringLiteral("AA:BB"), std::array{ soil(42.0, when.addSecs(60)) }).has_value());

        const QDateTime at = when.addSecs(120);
        const QList<PlantSensorBinding> b1 = bindings.activeFor(p1, at);
        const QList<PlantSensorBinding> b2 = bindings.activeFor(p2, at);
        const QList<Reading> c1 = readings.currentForPlant(std::span(b1.cbegin(), b1.cend()));
        const QList<Reading> c2 = readings.currentForPlant(std::span(b2.cbegin(), b2.cend()));
        QCOMPARE(c1.size(), 1);
        QCOMPARE(c2.size(), 1);
        QCOMPARE(c1.first().value.value(), 42.0);
        QCOMPARE(c2.first().value.value(), 42.0);
    }

    // Process-global gate: ingesting from sensor B does not reset sensor A's dedup state (the bug
    // the per-PlantCareModel gate had on plant navigation).
    void gateIsPerSensorNotReset()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryReadingRepository readings;
        ReadingIngester ingester(sensors, readings, m_clock);
        sensors.ensure(HandleKind::Mac, QStringLiteral("A"), QStringLiteral("a"));
        sensors.ensure(HandleKind::Mac, QStringLiteral("B"), QStringLiteral("b"));

        const QDateTime t = t0();
        QVERIFY(ingester.ingest(HandleKind::Mac, QStringLiteral("A"), std::array{ soil(40.0, t) }).has_value());
        // A different sensor's broadcast in between...
        QVERIFY(ingester.ingest(HandleKind::Mac, QStringLiteral("B"), std::array{ soil(99.0, t) }).has_value());
        // ...must NOT have reset A's gate: A's same value/bucket is still a redundant repeat.
        QCOMPARE(ingester.ingest(HandleKind::Mac, QStringLiteral("A"), std::array{ soil(40.0, t.addSecs(5)) }), std::nullopt);
    }
};

QTEST_MAIN(TestReadingIngester)
#include "test_readingingester.moc"
