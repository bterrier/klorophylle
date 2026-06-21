// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "device_esp32_airqualitymonitor.h"
#include "device_esp32_geigercounter.h"
#include "device_esp32_higrow.h"
#include "device_ess_generic.h"
#include "device_flowerpower.h"
#include "device_hygrotemp_clock.h"
#include "device_hygrotemp_square.h"
#include "device_parrotpot.h"
#include "device_wp6003.h"

#include <cmath>

using namespace klr;

// The per-device GATT realtime decoders are pure functions (bytes -> Readings),
// so they unit-test without any Bluetooth stack — the same seam as the
// advertisement decoders.
class TestGattDecoders : public QObject {
    Q_OBJECT

    static QByteArray b(std::initializer_list<int> xs)
    {
        QByteArray a;
        for (int x : xs)
            a.append(char(x));
        return a;
    }
    // Find the reading for a quantity; returns NaN if absent.
    static double val(const std::vector<Reading> &rs, Quantity q)
    {
        for (const Reading &r : rs)
            if (r.quantity == q && r.value)
                return *r.value;
        return std::nan("");
    }
    static bool near(double a, double e) { return std::abs(a - e) < 1e-6; }

private slots:
    void higrow()
    {
        const auto r = DeviceEsp32HiGrow::decodeRealtime(
            b({ 0x2a, 0x10, 0x00, 0x00, 0x00, 0xe6, 0x00, 0x37, 0x10, 0x27, 0x00, 0, 0, 0, 0, 0 }),
            QDateTime());
        QVERIFY(near(val(r, Quantity::SoilMoisture), 42));
        QVERIFY(near(val(r, Quantity::SoilConductivity), 16));
        QVERIFY(near(val(r, Quantity::AirTemperature), 23.0));
        QVERIFY(near(val(r, Quantity::AirHumidity), 55));
        QVERIFY(near(val(r, Quantity::Illuminance), 10000));
        // wrong-length frame yields nothing
        QVERIFY(DeviceEsp32HiGrow::decodeRealtime(b({ 1, 2, 3 }), QDateTime()).empty());
    }

    void airQuality()
    {
        const auto r = DeviceEsp32AirQualityMonitor::decodeRealtime(
            b({ 0xe6, 0x00, 0x37, 0xe8, 0x03, 0x64, 0x00, 0x90, 0x01, 0, 0, 0, 0, 0, 0, 0 }),
            QDateTime());
        QVERIFY(near(val(r, Quantity::AirTemperature), 23.0));
        QVERIFY(near(val(r, Quantity::AirHumidity), 55));
        QVERIFY(near(val(r, Quantity::Pressure), 1000));
        QVERIFY(near(val(r, Quantity::Voc), 100));
        QVERIFY(near(val(r, Quantity::Co2), 400));
    }

    void geiger()
    {
        const auto r = DeviceEsp32GeigerCounter::decodeRealtime(QByteArrayLiteral("12.5"), QDateTime());
        QCOMPARE(int(r.size()), 1);
        QVERIFY(near(val(r, Quantity::Radioactivity), 12.5));
    }

    void wp6003()
    {
        // [0]=0x0a data marker; big-endian fields; voc/hcho below 16383 so included.
        const auto r = DeviceWP6003::decodeRealtime(
            b({ 0x0a, 24, 1, 1, 12, 0, 0x00, 0xe6, 0, 0, 0x00, 0x64, 0x00, 0x32, 0, 0, 0x01, 0x90 }),
            QDateTime());
        QVERIFY(near(val(r, Quantity::AirTemperature), 23.0));
        QVERIFY(near(val(r, Quantity::Co2), 400));
        QVERIFY(near(val(r, Quantity::Voc), 100));
        QVERIFY(near(val(r, Quantity::Hcho), 50));
        // an ack frame (not 0x0a) decodes to nothing
        QVERIFY(DeviceWP6003::decodeRealtime(b({ 0xaa, 0, 0 }), QDateTime()).empty());
    }

    void square()
    {
        const auto r = DeviceHygrotempSquare::decodeRealtime(b({ 0xfa, 0x08, 0x37, 0xb8, 0x0b }), QDateTime());
        QVERIFY(near(val(r, Quantity::AirTemperature), 22.98));
        QVERIFY(near(val(r, Quantity::AirHumidity), 55));
        QVERIFY(near(val(r, Quantity::Battery), 89)); // int((3.000V - 2.1) * 100), float-truncated
    }

    void clock()
    {
        const auto r = DeviceHygrotempClock::decodeRealtime(b({ 0xfa, 0x08, 0x37 }), QDateTime());
        QVERIFY(near(val(r, Quantity::AirTemperature), 22.98));
        QVERIFY(near(val(r, Quantity::AirHumidity), 55));
    }

    void ess()
    {
        QVERIFY(near(val(DeviceEssGeneric::decodeTemperature(b({ 0xfa, 0x08 }), QDateTime()),
                         Quantity::AirTemperature), 22.98));
        QVERIFY(near(val(DeviceEssGeneric::decodeHumidity(b({ 0x70, 0x17 }), QDateTime()),
                         Quantity::AirHumidity), 60.0));
        // raw 0x000F7642 = 1013314 (0.1 Pa units) -> 1013.314 hPa
        QVERIFY(near(val(DeviceEssGeneric::decodePressure(b({ 0x42, 0x76, 0x0f, 0x00 }), QDateTime()),
                         Quantity::Pressure), 1013.314));
    }

    void flowerPower()
    {
        // conductivity = round(raw / 1.771); raw 1771 -> 1000
        QVERIFY(near(val(DeviceFlowerPower::decodeConductivity(b({ 0xeb, 0x06 }), QDateTime()),
                         Quantity::SoilConductivity), 1000));
        // air temperature is clamped to 55 for an out-of-range raw
        QVERIFY(near(val(DeviceFlowerPower::decodeAirTemperature(b({ 0xff, 0xff }), QDateTime()),
                         Quantity::AirTemperature), 55));
    }

    void parrotPot()
    {
        // mapNumber(raw, 2036, 1500, 0, 1000): raw 1768 -> ~500
        QVERIFY(near(val(DeviceParrotPot::decodeConductivity(b({ 0xe8, 0x06 }), QDateTime()),
                         Quantity::SoilConductivity), 500));
        QVERIFY(near(val(DeviceParrotPot::decodeWaterTank(b({ 0x4b }), QDateTime()),
                         Quantity::WaterTank), 75));
    }
};

QTEST_GUILESS_MAIN(TestGattDecoders)
#include "test_gattdecoders.moc"
