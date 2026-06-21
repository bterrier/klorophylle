// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cstdint>

namespace klr {

// LilyGo "HiGrow" ESP32 DIY plant sensor. No advertisement values — connect to
// the custom data service and subscribe to the 16-byte realtime characteristic.
// Ported from WatchFlower's device_esp32_higrow.cpp.
class DeviceEsp32HiGrow final : public Device {
public:
    QString model() const override { return QStringLiteral("HiGrow"); }
    DeviceType type() const override { return DeviceType::PlantSensor; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // connection-only
    }

    // 16-byte realtime frame (little-endian).
    static std::vector<Reading> decodeRealtime(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() == 16) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            out.push_back({ Quantity::SoilMoisture, double(d[0]), Unit::Percent, at, Provenance::Probe });
            out.push_back({ Quantity::SoilConductivity, double(d[1] + (d[2] << 8)), Unit::MicroSiemensPerCm, at, Provenance::Probe });
            out.push_back({ Quantity::AirTemperature, (d[5] + (d[6] << 8)) / 10.0, Unit::DegreeCelsius, at, Provenance::Probe });
            out.push_back({ Quantity::AirHumidity, double(d[7]), Unit::Percent, at, Provenance::Probe });
            out.push_back({ Quantity::Illuminance, double(d[8] + (d[9] << 8) + (d[10] << 16)), Unit::Lux, at, Provenance::Probe });
        }
        return out;
    }

    static std::vector<Reading> decodeBattery(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() == 1)
            out.push_back({ Quantity::Battery, double(static_cast<quint8>(v[0])), Unit::Percent, at, Provenance::Probe });
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("eeee9a32-a000-4cbd-b00b-6b519bf2780f");
        p.reads = {
            { QStringLiteral("eeee9a32-a0c0-4cbd-b00b-6b519bf2780f"),
              GattCharacteristicRead::Access::Notify, &DeviceEsp32HiGrow::decodeRealtime, {} },
            { QStringLiteral("00002a19-0000-1000-8000-00805f9b34fb"),
              GattCharacteristicRead::Access::Read, &DeviceEsp32HiGrow::decodeBattery,
              QStringLiteral("0000180f-0000-1000-8000-00805f9b34fb") },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("HiGrow"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
