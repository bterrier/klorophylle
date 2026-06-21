// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cstdint>

namespace klr {

// ESP32 DIY "AirQualityMonitor". Connection-only; subscribe to the 16-byte
// realtime characteristic. Ported from device_esp32_airqualitymonitor.cpp.
class DeviceEsp32AirQualityMonitor final : public Device {
public:
    QString model() const override { return QStringLiteral("AirQualityMonitor"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

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
            out.push_back({ Quantity::AirTemperature, (d[0] + (d[1] << 8)) / 10.0, Unit::DegreeCelsius, at, Provenance::Probe });
            out.push_back({ Quantity::AirHumidity, double(d[2]), Unit::Percent, at, Provenance::Probe });
            out.push_back({ Quantity::Pressure, double(d[3] + (d[4] << 8)), Unit::Hectopascal, at, Provenance::Probe });
            out.push_back({ Quantity::Voc, double(d[5] + (d[6] << 8)), Unit::MicrogramPerCubicMetre, at, Provenance::Probe });
            out.push_back({ Quantity::Co2, double(d[7] + (d[8] << 8)), Unit::Ppm, at, Provenance::Probe });
        }
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("eeee9a32-a000-4cbd-b00b-6b519bf2780f");
        p.reads = {
            { QStringLiteral("eeee9a32-a0a0-4cbd-b00b-6b519bf2780f"),
              GattCharacteristicRead::Access::Notify, &DeviceEsp32AirQualityMonitor::decodeRealtime, {} },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("AirQualityMonitor"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
