// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Generic fallback for any other (unencrypted) Xiaomi MiBeacon broadcaster on
// service 0xFE95 — Mi thermometers, etc. Registered AFTER the specific models so
// Flower Care / RoPot win first.
class DeviceXiaomi final : public Device {
public:
    QString model() const override { return QStringLiteral("Xiaomi sensor"); }
    DeviceType type() const override { return DeviceType::Generic; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.adv.serviceData16.contains(0xFE95);
    }
};

} // namespace klr
