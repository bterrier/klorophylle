// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Xiaomi Honeywell JQJCY01YM formaldehyde (HCHO) air-quality monitor. Broadcasts
// MiBeacon (0xFE95) carrying temperature, humidity and HCHO; recognised by its
// advertised name "JQJCY01YM". See ../../docs/jqjcy01ym-ble-api.md.
class DeviceJQJCY01YM final : public Device {
public:
    QString model() const override { return QStringLiteral("JQJCY01YM"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("JQJCY01YM"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
