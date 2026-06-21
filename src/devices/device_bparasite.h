// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// b-parasite: an open-source DIY soil sensor that broadcasts BTHome (v2 on
// service 0xFCD2, v1 on 0x181C). Recognised by its advertised name "bparasite";
// decoding reuses the BTHome parser. See ../../docs/bparasite-ble-api.md.
class DeviceBParasite final : public Device {
public:
    QString model() const override { return QStringLiteral("b-parasite"); }
    DeviceType type() const override { return DeviceType::PlantSensor; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        for (const quint16 uuid : { quint16(0xFCD2), quint16(0x181C) }) {
            if (adv.serviceData16.contains(uuid))
                return toReadings(AdvertisementParser::decodeBtHome(uuid, adv.serviceData16.value(uuid)), at);
        }
        return {};
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("bparasite"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
