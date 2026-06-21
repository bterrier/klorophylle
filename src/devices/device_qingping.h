// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping (Cleargrass) sensors broadcasting on service 0xFDCD — thermo-hygrometers,
// air monitors. See ../../docs/qingping-ble-api.md.
class DeviceQingping final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping sensor"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.adv.serviceData16.contains(0xFDCD);
    }
};

} // namespace klr
