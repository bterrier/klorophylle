// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "device_flowercare.h"
#include "device_ropot.h"

#include <cmath>

using namespace klr;

// The Flower Care / RoPot history GATT decoders are pure (bytes -> Readings), so they unit-test
// with no Bluetooth stack — the same seam as the advertisement + realtime decoders. Byte vectors
// are the worked examples in ../docs/flowercare-ble-api.md.
class TestHistoryDecoders : public QObject {
    Q_OBJECT

    static QByteArray b(std::initializer_list<int> xs)
    {
        QByteArray a;
        for (int x : xs)
            a.append(char(x));
        return a;
    }
    static double val(const std::vector<Reading> &rs, Quantity q)
    {
        for (const Reading &r : rs)
            if (r.quantity == q && r.value)
                return *r.value;
        return std::nan("");
    }
    static bool near(double a, double e) { return std::abs(a - e) < 1e-6; }

private slots:
    void countAndDeviceTime()
    {
        // doc "entry count": 0x2b007b04... -> uint16_le 43
        QCOMPARE(DeviceFlowerCare::decodeHistoryCount(
                     b({ 0x2b, 0x00, 0x7b, 0x04, 0xba, 0x13, 0x08, 0x00, 0xc8, 0x15, 0x08, 0, 0, 0, 0, 0 })),
                 43);
        QCOMPARE(DeviceFlowerCare::decodeHistoryCount(b({ 0x05 })), 0); // too short

        // doc "device time": 0x09ef2000 -> uint32_le 2158345 (seconds since boot)
        QCOMPARE(DeviceFlowerCare::decodeDeviceTimeSecs(b({ 0x09, 0xef, 0x20, 0x00 })),
                 qint64(2158345));
        QCOMPARE(DeviceFlowerCare::decodeDeviceTimeSecs(b({ 1, 2, 3 })), qint64(0)); // too short
    }

    void entryDecodesAllFourQuantitiesAtAbsoluteTime()
    {
        // doc "read entry": 0x70e72000 eb00 00 5a000000 15 b300 0000
        // uptime 2156400s, temp 23.5°C, lux 90, moisture 21%, conductivity 179 µS/cm.
        const qint64 wallEpochMs = 1'600'000'000'000; // arbitrary boot wall-clock
        const auto r = DeviceFlowerCare::decodeHistoryEntry(
            b({ 0x70, 0xe7, 0x20, 0x00, 0xeb, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x15, 0xb3, 0x00, 0x00, 0x00 }),
            wallEpochMs);

        QCOMPARE(r.size(), size_t(4));
        QVERIFY(near(val(r, Quantity::AirTemperature), 23.5));
        QVERIFY(near(val(r, Quantity::Illuminance), 90));
        QVERIFY(near(val(r, Quantity::SoilMoisture), 21));
        QVERIFY(near(val(r, Quantity::SoilConductivity), 179));

        // Absolute timestamp = boot wall-clock + the entry's uptime seconds.
        const qint64 expected = wallEpochMs + qint64(2156400) * 1000;
        QCOMPARE(r.front().timestamp.toMSecsSinceEpoch(), expected);
        QCOMPARE(int(r.front().provenance), int(Provenance::History));

        QVERIFY(DeviceFlowerCare::decodeHistoryEntry(b({ 1, 2, 3 }), wallEpochMs).empty());
    }

    void negativeTemperature()
    {
        // temp bytes 0x38ff = -200 (int16 LE) -> -20.0 °C
        const auto r = DeviceFlowerCare::decodeHistoryEntry(
            b({ 0, 0, 0, 0, 0x38, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }), 0);
        QVERIFY(near(val(r, Quantity::AirTemperature), -20.0));
    }

    void battery()
    {
        const auto r = DeviceFlowerCare::decodeBattery(b({ 0x55, 0x00, 0x33, 0x2e, 0x37 }), QDateTime());
        QVERIFY(near(val(r, Quantity::Battery), 85)); // 0x55
        QCOMPARE(int(r.front().provenance), int(Provenance::Probe));
        QVERIFY(DeviceFlowerCare::decodeBattery(QByteArray(), QDateTime()).empty());
    }

    void profilesPopulatedAndAddressEncoding()
    {
        const auto fc = DeviceFlowerCare().gattHistoryProfile();
        QVERIFY(fc.has_value());
        QCOMPARE(fc->productId, quint16(0x0098));
        QVERIFY(fc->needsHandshake);
        QCOMPARE(fc->modeCommand, QByteArray::fromHex("a00000"));
        QVERIFY(bool(fc->decodeEntry) && bool(fc->decodeCount) && bool(fc->decodeBattery));
        // Entry-address encoding: 0xa1 + index as two LE bytes.
        QCOMPARE(fc->addressFor(0), QByteArray::fromHex("a10000"));
        QCOMPARE(fc->addressFor(1), QByteArray::fromHex("a10100"));
        QCOMPARE(fc->addressFor(16), QByteArray::fromHex("a11000"));
        QCOMPARE(fc->addressFor(258), QByteArray::fromHex("a10201"));

        // RoPot shares the format, differing only by product id.
        const auto ro = DeviceRoPot().gattHistoryProfile();
        QVERIFY(ro.has_value());
        QCOMPARE(ro->productId, quint16(0x015D));
        QCOMPARE(ro->service, fc->service);
    }
};

QTEST_APPLESS_MAIN(TestHistoryDecoders)
#include "test_historydecoders.moc"
