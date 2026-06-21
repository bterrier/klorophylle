// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping CGP1W ("Qingping Temp RH Barometer"): a thermo-hygrometer with a
// barometer, broadcasting on the Qingping service 0xFDCD (adds air pressure to
// temperature + humidity). See ../../docs/cgp1w-ble-api.md.
class DeviceQingpingCGP1W final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping Temp RH Barometer"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("Qingping Temp RH Barometer"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
