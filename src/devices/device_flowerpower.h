// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cmath>
#include <cstdint>

namespace klr {

// Parrot Flower Power (advertised name "Flower power…"). Connection-only: read
// five raw uint16 characteristics from the live service and apply Parrot's
// calibration curves. See docs/flowerpower-ble-api.md.
class DeviceFlowerPower final : public Device {
public:
    QString model() const override { return QStringLiteral("Flower Power"); }
    DeviceType type() const override { return DeviceType::PlantSensor; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // connection-only
    }

    static uint16_t raw16(const QByteArray &v)
    {
        if (v.size() < 2)
            return 0;
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        return static_cast<uint16_t>(d[0] + (d[1] << 8));
    }

    // Parrot's published air/soil-temperature curve (shared by Flower Power & Pot).
    static double tempCurve(double v)
    {
        return 0.00000003044 * std::pow(v, 3.0) - 0.00008038 * std::pow(v, 2.0) + v * 0.1149 - 30.45;
    }

    static std::vector<Reading> decodeSunlight(const QByteArray &v, const QDateTime &at)
    {
        const double raw = raw16(v);
        const double lux = std::round(1000.0 * 0.0864 * (192773.17 * std::pow(raw, -1.0606619)));
        return { { Quantity::Illuminance, lux, Unit::Lux, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeConductivity(const QByteArray &v, const QDateTime &at)
    {
        return { { Quantity::SoilConductivity, std::round(raw16(v) / 1.771), Unit::MicroSiemensPerCm, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeSoilTemperature(const QByteArray &v, const QDateTime &at)
    {
        return { { Quantity::SoilTemperature, tempCurve(raw16(v)), Unit::DegreeCelsius, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeAirTemperature(const QByteArray &v, const QDateTime &at)
    {
        double t = tempCurve(raw16(v));
        t = t < -10.0 ? -10.0 : (t > 55.0 ? 55.0 : t);
        return { { Quantity::AirTemperature, t, Unit::DegreeCelsius, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeSoilMoisture(const QByteArray &v, const QDateTime &at)
    {
        const double raw = raw16(v);
        const double h1 = 11.4293
            + (0.0000000010698 * std::pow(raw, 4.0) - 0.00000152538 * std::pow(raw, 3.0)
               + 0.000866976 * std::pow(raw, 2.0) - 0.169422 * raw);
        double h2 = 100.0 * (0.0000045 * std::pow(h1, 3.0) - 0.00055 * std::pow(h1, 2.0) + 0.0292 * h1 - 0.053);
        h2 = h2 < 0.0 ? 0.0 : (h2 > 60.0 ? 60.0 : h2);
        return { { Quantity::SoilMoisture, std::round(h2), Unit::Percent, at, Provenance::Probe } };
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("39e1fa00-84a8-11e2-afba-0002a5d5c51b");
        p.reads = {
            { QStringLiteral("39e1fa01-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceFlowerPower::decodeSunlight, {} },
            { QStringLiteral("39e1fa02-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceFlowerPower::decodeConductivity, {} },
            { QStringLiteral("39e1fa03-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceFlowerPower::decodeSoilTemperature, {} },
            { QStringLiteral("39e1fa04-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceFlowerPower::decodeAirTemperature, {} },
            { QStringLiteral("39e1fa05-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceFlowerPower::decodeSoilMoisture, {} },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.startsWith(QLatin1String("Flower power"), Qt::CaseInsensitive);
    }
};

} // namespace klr
