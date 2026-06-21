// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "clock.h"
#include "format.h"
#include "reading.h"

using namespace klr;

// Pure-value-type + formatting tests: no SQLite, no BLE, no QML — the testability
// WatchFlower lacked.
class TestReading : public QObject {
    Q_OBJECT

private slots:
    void absentIsNulloptNotSentinel()
    {
        Reading r;
        QVERIFY(!r.value.has_value()); // absent == std::nullopt, never -99
        QCOMPARE(formatValue(r), QStringLiteral("—"));
    }

    void formatsValueWithUnit()
    {
        const Reading r { Quantity::SoilMoisture, 42.0, Unit::Percent, {}, Provenance::Live };
        QCOMPARE(formatValue(r), QStringLiteral("42.0 %"));
    }

    void integerUnitsHaveNoDecimals()
    {
        const Reading lux { Quantity::Illuminance, 1200.0, Unit::Lux, {}, Provenance::Live };
        QCOMPARE(formatValue(lux), QStringLiteral("1200 lux"));
    }

    void labelsAreStable()
    {
        QCOMPARE(label(Quantity::AirTemperature), QStringLiteral("Temperature"));
        QVERIFY(!label(Quantity::Illuminance).isEmpty());
    }

    void fakeClockIsDeterministic()
    {
        FakeClock c;
        c.t = 1234;
        QCOMPARE(c.nowMs(), qint64(1234));
        c.t = 5678;
        QCOMPARE(c.nowMs(), qint64(5678));
    }

    void systemClockIsPositive()
    {
        SystemClock c;
        QVERIFY(c.nowMs() > 0);
    }
};

QTEST_GUILESS_MAIN(TestReading)
#include "test_reading.moc"
