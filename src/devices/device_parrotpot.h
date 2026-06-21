// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace klr {

// Parrot Pot (advertised name "Parrot pot…"). Connection-only: like Flower Power
// but uses the calibrated float characteristics for moisture/sunlight, plus a
// water-tank level from a separate watering service. See docs/parrotpot-ble-api.md.
class DeviceParrotPot final : public Device {
public:
    QString model() const override { return QStringLiteral("Parrot Pot"); }
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
    static float rawFloat(const QByteArray &v)
    {
        float f = 0.f;
        if (v.size() >= 4)
            std::memcpy(&f, v.constData(), 4); // little-endian float32
        return f;
    }
    static double mapNumber(double v, double inMin, double inMax, double outMin, double outMax)
    {
        return outMin + (v - inMin) * (outMax - outMin) / (inMax - inMin);
    }

    static std::vector<Reading> decodeConductivity(const QByteArray &v, const QDateTime &at)
    {
        return { { Quantity::SoilConductivity, std::round(mapNumber(raw16(v), 2036, 1500, 0, 1000)),
                   Unit::MicroSiemensPerCm, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeSoilTemperature(const QByteArray &v, const QDateTime &at)
    {
        const double raw = raw16(v);
        const double t = 0.00000003044 * std::pow(raw, 3.0) - 0.00008038 * std::pow(raw, 2.0) + raw * 0.1149 - 30.45;
        return { { Quantity::SoilTemperature, t, Unit::DegreeCelsius, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeAirTemperature(const QByteArray &v, const QDateTime &at)
    {
        const double raw = raw16(v);
        const double t = 0.00000003044 * std::pow(raw, 3.0) - 0.00008038 * std::pow(raw, 2.0) + raw * 0.1149 - 30.45;
        return { { Quantity::AirTemperature, t, Unit::DegreeCelsius, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeSoilMoisture(const QByteArray &v, const QDateTime &at)
    {
        return { { Quantity::SoilMoisture, std::round(rawFloat(v)), Unit::Percent, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeSunlight(const QByteArray &v, const QDateTime &at)
    {
        return { { Quantity::Illuminance, std::round(rawFloat(v) * 11.574 * 53.93 * 10.0), Unit::Lux, at, Provenance::Probe } };
    }
    static std::vector<Reading> decodeWaterTank(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() >= 1)
            out.push_back({ Quantity::WaterTank, double(static_cast<quint8>(v[0])), Unit::Percent, at, Provenance::Probe });
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        const QString watering = QStringLiteral("39e1f900-84a8-11e2-afba-0002a5d5c51b");
        GattReadProfile p;
        p.service = QStringLiteral("39e1fa00-84a8-11e2-afba-0002a5d5c51b");
        p.reads = {
            { QStringLiteral("39e1fa02-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeConductivity, {} },
            { QStringLiteral("39e1fa03-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeSoilTemperature, {} },
            { QStringLiteral("39e1fa04-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeAirTemperature, {} },
            { QStringLiteral("39e1fa09-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeSoilMoisture, {} },
            { QStringLiteral("39e1fa0b-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeSunlight, {} },
            { QStringLiteral("39e1f907-84a8-11e2-afba-0002a5d5c51b"), GattCharacteristicRead::Access::Read, &DeviceParrotPot::decodeWaterTank, watering },
        };
        return p;
    }

    // --- Write commands (ADR 0026) -------------------------------------------------------
    // Ported from WatchFlower's src/devices/device_parrotpot.cpp, cross-checked against
    // docs/parrotpot-ble-api.md. LED lives on the live service, watering on the watering service.
    static QByteArray ledBlinkPayload() { return QByteArray::fromHex("01"); }   // 39e1fa07
    static QByteArray wateringPayload() { return QByteArray::fromHex("0800"); } // 39e1f906

    QList<GattCommand> gattCommands(const QDateTime &) const override
    {
        return {
            { DeviceAction::LedBlink,
              QStringLiteral("39e1fa00-84a8-11e2-afba-0002a5d5c51b"),
              QStringLiteral("39e1fa07-84a8-11e2-afba-0002a5d5c51b"),
              ledBlinkPayload(), /*writeWithoutResponse*/ false, std::nullopt },
            { DeviceAction::Watering,
              QStringLiteral("39e1f900-84a8-11e2-afba-0002a5d5c51b"),
              QStringLiteral("39e1f906-84a8-11e2-afba-0002a5d5c51b"),
              wateringPayload(), /*writeWithoutResponse*/ false, std::nullopt },
        };
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.startsWith(QLatin1String("Parrot pot"), Qt::CaseInsensitive);
    }
};

} // namespace klr
