// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cstdint>

namespace klr {

// Xiaomi "square" hygrometers on stock firmware: LYWSD03MMC, MHO-C401,
// XMWSDJO4MMC, MJWSD05MMC. Their advertisement is encrypted MiBeacon (not
// decoded), so values come over GATT: subscribe to the 5-byte Temp&Humi
// characteristic. Battery is derived from the reported coin-cell voltage.
// See docs/lywsd03mmc-ble-api.md.
class DeviceHygrotempSquare final : public Device {
public:
    QString model() const override { return QStringLiteral("LYWSD03MMC"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // stock-firmware advertisement is encrypted; read over GATT
    }

    // 5-byte frame: temp (int16 LE / 100), humidity (uint8), voltage (uint16 LE / 1000).
    static std::vector<Reading> decodeRealtime(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() == 5) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            out.push_back({ Quantity::AirTemperature, static_cast<int16_t>(d[0] + (d[1] << 8)) / 100.0,
                            Unit::DegreeCelsius, at, Provenance::Probe });
            out.push_back({ Quantity::AirHumidity, double(d[2]), Unit::Percent, at, Provenance::Probe });

            const double voltage = static_cast<int16_t>(d[3] + (d[4] << 8)) / 1000.0;
            int battery = static_cast<int>((voltage - 2.1) * 100.0);
            battery = battery < 0 ? 0 : (battery > 100 ? 100 : battery);
            out.push_back({ Quantity::Battery, double(battery), Unit::Percent, at, Provenance::Probe });
        }
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
        p.reads = {
            { QStringLiteral("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6"),
              GattCharacteristicRead::Access::Notify, &DeviceHygrotempSquare::decodeRealtime, {} },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("LYWSD03MMC"), Qt::CaseInsensitive) == 0
            || c.name.compare(QLatin1String("MHO-C401"), Qt::CaseInsensitive) == 0
            || c.name.compare(QLatin1String("XMWSDJO4MMC"), Qt::CaseInsensitive) == 0
            || c.name.compare(QLatin1String("MJWSD05MMC"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
