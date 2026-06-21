// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "advertisementparser.h"

#include <cmath>

using namespace klr;

// Exercises the ported AdvertisementParser (proven WatchFlower decoders), the
// SensorAdvData -> Reading adapter, and the snapshot accumulator. Vectors are the
// worked examples from docs/{mibeacon,bthome,qingping}-ble-api.md.
class TestAdvertisement : public QObject {
    Q_OBJECT

    static QByteArray f(const char *hex) { return QByteArray::fromHex(hex); }
    static bool nf(float v, float e) { return std::abs(v - e) < 0.05f; }
    static bool nd(std::optional<double> v, double e) { return v.has_value() && std::abs(*v - e) < 1e-6; }

private slots:
    // ---- AdvertisementParser: MiBeacon (Flower Care) ----
    void xiaomiSoilMoisture()
    {
        const auto d = AdvertisementParser::decodeXiaomi(0xFE95, f("71209800710000668d7cc40d0810011f"));
        QCOMPARE(d.soilMoisture, 31);
    }
    void xiaomiTemperature()
    {
        const auto d = AdvertisementParser::decodeXiaomi(0xFE95, f("71209800710000668d7cc40d041002e600"));
        QVERIFY(nf(d.temperature, 23.0f));
    }
    void xiaomiConductivity()
    {
        const auto d = AdvertisementParser::decodeXiaomi(0xFE95, f("71209800710000668d7cc40d0910020101"));
        QCOMPARE(d.soilConductivity, 257);
    }
    void xiaomiRejectsWrongUuid()
    {
        const auto d = AdvertisementParser::decodeXiaomi(0x1234, f("71209800710000668d7cc40d0810011f"));
        QCOMPARE(d.soilMoisture, -99);
    }

    // ---- AdvertisementParser: BTHome v2 ----
    void btHomeTemperature()
    {
        const auto d = AdvertisementParser::decodeBtHome(0xFCD2, f("4002ca09"));
        QVERIFY(nf(d.temperature, 25.06f));
    }
    void btHomeBattery()
    {
        const auto d = AdvertisementParser::decodeBtHome(0xFCD2, f("400161"));
        QCOMPARE(d.battery, 97);
    }

    // ---- AdvertisementParser: Qingping ----
    void qingpingTempHumidityBattery()
    {
        const auto d = AdvertisementParser::decodeQingping(
            0xFDCD,
            f("0809" "0000" "40342d58" "01" "04" "08018702" "07" "02" "4f27" "02" "01" "5c"));
        QVERIFY(nf(d.temperature, 26.4f));
        QVERIFY(nf(d.humidity, 64.7f));
        QCOMPARE(d.battery, 92);
    }

    // ---- objectMask ----
    void objectMaskReflectsFields()
    {
        SensorAdvData d;
        d.soilMoisture = 30;
        d.temperature = 21.f;
        const unsigned m = AdvertisementParser::objectMask(d);
        QVERIFY(m & AdvertisementParser::ADV_MOISTURE);
        QVERIFY(m & AdvertisementParser::ADV_TEMPERATURE);
        QVERIFY(!(m & AdvertisementParser::ADV_BATTERY));
    }

    // ---- AdvSnapshotAccumulator ----
    void accumulatorCompletesWithinWindow()
    {
        AdvSnapshotAccumulator acc;
        acc.requiredMask = AdvertisementParser::ADV_TEMPERATURE | AdvertisementParser::ADV_MOISTURE;
        acc.windowMs = 1000;
        QCOMPARE(int(acc.feed(AdvertisementParser::ADV_TEMPERATURE, 0)), int(AdvSnapshotAccumulator::Partial));
        QCOMPARE(int(acc.feed(AdvertisementParser::ADV_MOISTURE, 500)), int(AdvSnapshotAccumulator::Complete));
    }
    void accumulatorRestartsAfterWindow()
    {
        AdvSnapshotAccumulator acc;
        acc.requiredMask = AdvertisementParser::ADV_TEMPERATURE | AdvertisementParser::ADV_MOISTURE;
        acc.windowMs = 100;
        acc.feed(AdvertisementParser::ADV_TEMPERATURE, 0);
        // The second bit arrives past the window -> sequence restarts, still Partial.
        QCOMPARE(int(acc.feed(AdvertisementParser::ADV_MOISTURE, 1000)), int(AdvSnapshotAccumulator::Partial));
    }

};

QTEST_GUILESS_MAIN(TestAdvertisement)
#include "test_advertisement.moc"
