// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Xiaomi Mijia LYWSDCGQ thermo-hygrometer (the round "MJ_HT_V1"). Broadcasts
// MiBeacon (0xFE95) with temperature + humidity; recognised by its advertised
// name "MJ_HT_V1". See ../../docs/lywsdcgq-ble-api.md.
class DeviceHygrotempLYWSDCGQ final : public Device {
public:
    QString model() const override { return QStringLiteral("MJ_HT_V1"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("MJ_HT_V1"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
