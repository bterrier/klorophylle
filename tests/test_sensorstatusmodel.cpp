// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "ids.h"
#include "inmemorybindingrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "reading.h"
#include "sensorstatusmodel.h"

#include <array>

using namespace klr;

// The per-plant bound-sensor status model (the plant-detail "Sensors" tab). Exercised
// with no BLE: rows are built from the bindings + sensors, battery comes from the reading
// store, and liveness is suppressed (-1, no dot) without a scanner. The Offline/Stale/Live
// transition matrix itself is owned by the pure livenessOf() (test_liveness).
class TestSensorStatusModel : public QObject {
    Q_OBJECT

    FakeClock m_clock; // epoch; bindings + activeFor align to it

    static QVariant role(const SensorStatusModel &m, int row, int r)
    {
        return m.data(m.index(row, 0), r);
    }

private slots:
    void emptyWithoutPlant()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorStatusModel m(sensors, bindings, readings, m_clock);
        QCOMPARE(m.rowCount(), 0);
    }

    void rowCarriesModelAddressBatteryAndSince()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorStatusModel m(sensors, bindings, readings, m_clock);

        const PlantId plant = PlantId::generate();
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s =
            sensors.ensure(HandleKind::Mac, QStringLiteral("AA:BB:CC:DD:EE:FF"),
                           QStringLiteral("Flower care"));
        bindings.bind(plant, s, now, std::nullopt);
        readings.append(s, std::array{ Reading{ Quantity::Battery, 80.0, Unit::Percent, now,
                                                 Provenance::History } });

        m.setPlant(plant);
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(role(m, 0, SensorStatusModel::ModelRole).toString(), QStringLiteral("Flower care"));
        QCOMPARE(role(m, 0, SensorStatusModel::AddressRole).toString(),
                 QStringLiteral("AA:BB:CC:DD:EE:FF"));
        QVERIFY(!role(m, 0, SensorStatusModel::SinceRole).toString().isEmpty());
        QVERIFY(role(m, 0, SensorStatusModel::BatteryRole).toString().contains(QStringLiteral("80")));
        // No scanner injected -> liveness is suppressed (no dot), GATT closed.
        QCOMPARE(role(m, 0, SensorStatusModel::LivenessRole).toInt(), -1);
        QCOMPARE(role(m, 0, SensorStatusModel::GattOpenRole).toBool(), false);
    }

    void batteryEmptyWhenNoReading()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorStatusModel m(sensors, bindings, readings, m_clock);

        const PlantId plant = PlantId::generate();
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);

        m.setPlant(plant);
        QCOMPARE(m.rowCount(), 1);
        QVERIFY(role(m, 0, SensorStatusModel::BatteryRole).toString().isEmpty());
    }

    void setPlantNulloptClears()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorStatusModel m(sensors, bindings, readings, m_clock);

        const PlantId plant = PlantId::generate();
        const QDateTime now = QDateTime::fromMSecsSinceEpoch(m_clock.nowMs(), QTimeZone::UTC);
        const SensorId s = sensors.ensure(HandleKind::Mac, QStringLiteral("AA"), QStringLiteral("m"));
        bindings.bind(plant, s, now, std::nullopt);
        m.setPlant(plant);
        QCOMPARE(m.rowCount(), 1);

        m.setPlant(std::nullopt);
        QCOMPARE(m.rowCount(), 0);
        m.refreshConnectivity(); // safe no-op with no rows / no scanner
    }

    void roleNamesAreUnique()
    {
        InMemorySensorRepository sensors(m_clock);
        InMemoryBindingRepository bindings;
        InMemoryReadingRepository readings;
        SensorStatusModel m(sensors, bindings, readings, m_clock);
        const QHash<int, QByteArray> roles = m.roleNames();
        QCOMPARE(roles.value(SensorStatusModel::AddressRole), QByteArray("address"));
        QCOMPARE(roles.value(SensorStatusModel::LivenessRole), QByteArray("liveness"));
        QCOMPARE(roles.value(SensorStatusModel::GattOpenRole), QByteArray("gattOpen"));
        // No two roles share a name.
        QSet<QByteArray> names;
        for (const QByteArray &n : roles.values())
            names.insert(n);
        QCOMPARE(names.size(), roles.size());
    }
};

QTEST_GUILESS_MAIN(TestSensorStatusModel)
#include "test_sensorstatusmodel.moc"
