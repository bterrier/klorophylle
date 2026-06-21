// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Reflashed Xiaomi thermometers (LYWSD03MMC, MHO-C401, …) running ATC1441 / pvvx
// custom firmware, configured to broadcast BTHome or MiBeacon. Recognised by the
// advertised name ("ATC_xxxx"); decoding reuses the BTHome / MiBeacon parsers.
//
// Note: the firmware's *native* atc1441 / pvvx format (service 0x181A) is not
// decoded here — as in WatchFlower, the device must be set to BTHome or MiBeacon
// output. A dedicated 0x181A decoder could be added later.
class DeviceAtc final : public Device {
public:
    QString model() const override { return QStringLiteral("ATC thermometer"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        for (const quint16 uuid : { quint16(0xFCD2), quint16(0x181C) }) {
            if (adv.serviceData16.contains(uuid))
                return toReadings(AdvertisementParser::decodeBtHome(uuid, adv.serviceData16.value(uuid)), at);
        }
        if (adv.serviceData16.contains(0xFE95))
            return toReadings(AdvertisementParser::decodeXiaomi(0xFE95, adv.serviceData16.value(0xFE95)), at);
        return {};
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.startsWith(QLatin1String("ATC"), Qt::CaseInsensitive);
    }
};

} // namespace klr
