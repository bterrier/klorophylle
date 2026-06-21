// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Any BTHome broadcaster: b-parasite, Shelly, ESPHome and DIY sensors. v2 uses
// service 0xFCD2; v1 uses 0x181C (0x181E = encrypted, unsupported). Generic by
// design — BTHome is a shared open format, not a single model.
// See ../../docs/bthome-ble-api.md.
class DeviceBtHome final : public Device {
public:
    QString model() const override { return QStringLiteral("BTHome sensor"); }
    DeviceType type() const override { return DeviceType::Generic; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        for (const quint16 uuid : { quint16(0xFCD2), quint16(0x181C), quint16(0x181E) }) {
            if (adv.serviceData16.contains(uuid))
                return toReadings(AdvertisementParser::decodeBtHome(uuid, adv.serviceData16.value(uuid)), at);
        }
        return {};
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.adv.serviceData16.contains(0xFCD2) || c.adv.serviceData16.contains(0x181C)
            || c.adv.serviceData16.contains(0x181E);
    }
};

} // namespace klr
