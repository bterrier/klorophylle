// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "device.h"
#include "deviceregistry.h"

#include <cmath>

using namespace klr;

// The device registry picks the matching Device subclass for a discovered
// advertisement; the subclass decodes it (delegating to the ported parser) and
// the SensorAdvData -> Reading adapter yields std::optional values.
class TestDevices : public QObject {
    Q_OBJECT

    static QByteArray f(const char *hex) { return QByteArray::fromHex(hex); }
    static bool nd(std::optional<double> v, double e) { return v.has_value() && std::abs(*v - e) < 1e-6; }
    static AdvertisementContext svc(quint16 uuid, const QByteArray &bytes)
    {
        AdvertisementContext c;
        c.adv.serviceData16.insert(uuid, bytes);
        return c;
    }

private slots:
    void registryPicksFlowerCareByProductId()
    {
        const auto reg = makeBuiltinRegistry();
        const auto ctx = svc(0xFE95, f("71209800710000668d7cc40d0810011f"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Flower Care"));
        QCOMPARE(int(dev->type()), int(DeviceType::PlantSensor));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 1);
        QCOMPARE(int(r[0].quantity), int(Quantity::SoilMoisture));
        QVERIFY(nd(r[0].value, 31.0));
    }

    void registryPicksRoPotByProductId()
    {
        const auto reg = makeBuiltinRegistry();
        const auto dev = reg.create(svc(0xFE95, f("71205d01710000668d7cc40d0810011f")));
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("RoPot"));
    }

    void registryFallsBackToGenericXiaomi()
    {
        const auto reg = makeBuiltinRegistry();
        const auto dev = reg.create(svc(0xFE95, f("7120ffff710000668d7cc40d0810011f")));
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Xiaomi sensor"));
    }

    void registryPicksBtHome()
    {
        const auto reg = makeBuiltinRegistry();
        const auto ctx = svc(0xFCD2, f("4002ca09"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("BTHome sensor"));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 1);
        QCOMPARE(int(r[0].quantity), int(Quantity::AirTemperature));
        QVERIFY(nd(r[0].value, 25.06));
    }

    void registryPicksQingping()
    {
        const auto reg = makeBuiltinRegistry();
        const auto ctx = svc(0xFDCD,
            f("0809" "0000" "40342d58" "01" "04" "08018702" "07" "02" "4f27" "02" "01" "5c"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Qingping sensor"));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QVERIFY(r.size() >= 3); // temperature + humidity + battery (+ pressure)
    }

    void registryPicksAtcByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("ATC_1a2b3c");
        ctx.adv.serviceData16.insert(0xFCD2, f("4002ca09")); // ATC firmware set to BTHome output
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("ATC thermometer"));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 1);
        QCOMPARE(int(r[0].quantity), int(Quantity::AirTemperature));
        QVERIFY(nd(r[0].value, 25.06));
    }

    void registryPicksThermoBeaconByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("ThermoBeacon");
        ctx.adv.manufacturerData.insert(0x0010, f("0000d72a0000" "0000" "bd0b" "6801" "8c02" "1cbe5900"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("ThermoBeacon"));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(int(r[0].quantity), int(Quantity::AirTemperature));
        QVERIFY(nd(r[0].value, 22.5));
        QCOMPARE(int(r[1].quantity), int(Quantity::AirHumidity));
        QVERIFY(nd(r[1].value, 41.0)); // round(40.75)
    }

    void registryPicksBParasiteByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("bparasite");
        ctx.adv.serviceData16.insert(0xFCD2, f("4002ca09")); // b-parasite broadcasts BTHome
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("b-parasite"));
        QCOMPARE(int(dev->type()), int(DeviceType::PlantSensor)); // not generic BTHome
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 1);
        QCOMPARE(int(r[0].quantity), int(Quantity::AirTemperature));
        QVERIFY(nd(r[0].value, 25.06));
    }

    void registryPicksJQJCY01YMByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("JQJCY01YM");
        ctx.adv.serviceData16.insert(0xFE95, f("7120ffff710000668d7cc40d041002e600")); // MiBeacon temperature, generic product id
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("JQJCY01YM"));
        QCOMPARE(int(dev->type()), int(DeviceType::AirQuality)); // not generic Xiaomi
    }

    void registryPicksMJHTV1ByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("MJ_HT_V1");
        ctx.adv.serviceData16.insert(0xFE95, f("7120ffff710000668d7cc40d041002e600")); // MiBeacon temperature, generic product id
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("MJ_HT_V1"));
        QCOMPARE(int(dev->type()), int(DeviceType::Thermometer)); // not generic Xiaomi
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QCOMPARE(int(r.size()), 1);
        QCOMPARE(int(r[0].quantity), int(Quantity::AirTemperature));
        QVERIFY(nd(r[0].value, 23.0));
    }

    void registryPicksCGDN1ByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("Qingping Air Monitor Lite");
        ctx.adv.serviceData16.insert(0xFDCD,
            f("0809" "0000" "40342d58" "01" "04" "08018702" "07" "02" "4f27" "02" "01" "5c"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Qingping Air Monitor Lite"));
        QCOMPARE(int(dev->type()), int(DeviceType::AirQuality)); // not generic Qingping (Thermometer)
    }

    void registryPicksQingpingThermometerByName()
    {
        const auto reg = makeBuiltinRegistry();
        AdvertisementContext ctx;
        ctx.name = QStringLiteral("Qingping Temp RH Lite"); // CGDK2
        ctx.adv.serviceData16.insert(0xFDCD,
            f("0809" "0000" "40342d58" "01" "04" "08018702" "07" "02" "4f27" "02" "01" "5c"));
        const auto dev = reg.create(ctx);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Qingping Temp RH Lite"));
        QCOMPARE(int(dev->type()), int(DeviceType::Thermometer));
        const auto r = dev->parseAdvertisement(ctx.adv, QDateTime());
        QVERIFY(r.size() >= 3); // same decode as the generic Qingping path
    }

    void registryReturnsNullForUnknownService()
    {
        const auto reg = makeBuiltinRegistry();
        QVERIFY(reg.create(svc(0x1234, f("deadbeef"))) == nullptr);
    }

    // --- Connection-only devices: matched by advertised name (no broadcast values),
    //     recognised with the right type and offering a GATT read profile. ---

    static std::unique_ptr<Device> byName(const QString &name)
    {
        AdvertisementContext c;
        c.name = name;
        return makeBuiltinRegistry().create(c);
    }

    void registryPicksConnectionDevicesByName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("model");
        QTest::addColumn<int>("type");
        QTest::newRow("flowerpower") << "Flower power 1234" << "Flower Power" << int(DeviceType::PlantSensor);
        QTest::newRow("parrotpot") << "Parrot pot abcd" << "Parrot Pot" << int(DeviceType::PlantSensor);
        QTest::newRow("higrow") << "HiGrow" << "HiGrow" << int(DeviceType::PlantSensor);
        QTest::newRow("aqm") << "AirQualityMonitor" << "AirQualityMonitor" << int(DeviceType::AirQuality);
        QTest::newRow("geiger") << "GeigerCounter" << "GeigerCounter" << int(DeviceType::AirQuality);
        QTest::newRow("wp6003") << "6003#0011223344" << "WP6003" << int(DeviceType::AirQuality);
        QTest::newRow("square") << "LYWSD03MMC" << "LYWSD03MMC" << int(DeviceType::Thermometer);
        QTest::newRow("clock") << "MHO-C303" << "LYWSD02" << int(DeviceType::Thermometer);
    }

    void registryPicksConnectionDevicesByName()
    {
        QFETCH(QString, name);
        QFETCH(QString, model);
        QFETCH(int, type);

        const auto dev = byName(name);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), model);
        QCOMPARE(int(dev->type()), type);
        QVERIFY(dev->gattProfile().has_value());            // offers a one-shot GATT read
        QVERIFY(dev->parseAdvertisement({}, QDateTime()).empty()); // but no broadcast values
    }

    void registryPicksEssByAdvertisedServiceUuid()
    {
        AdvertisementContext c;
        c.adv.serviceUuids16.append(quint16(0x181A)); // advertises the ESS service
        const auto dev = makeBuiltinRegistry().create(c);
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Environmental sensor"));
        QVERIFY(dev->gattProfile().has_value());
    }

    void advertisementOnlyDeviceHasNoGattProfile()
    {
        // Flower Care decodes from advertisements and needs no connection.
        const auto dev = makeBuiltinRegistry().create(svc(0xFE95, f("71209800710000668d7cc40d0810011f")));
        QVERIFY(dev != nullptr);
        QCOMPARE(dev->model(), QStringLiteral("Flower Care"));
        QVERIFY(!dev->gattProfile().has_value());
    }
};

QTEST_GUILESS_MAIN(TestDevices)
#include "test_devices.moc"
