// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping (Cleargrass) CGG1 thermo-hygrometer. Broadcasts on the Qingping
// service 0xFDCD; sold under two advertised names ("ClearGrass Temp & RH" and
// "Qingping Temp & RH M"). See ../../docs/cgg1-ble-api.md.
class DeviceQingpingCGG1 final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping Temp & RH"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("ClearGrass Temp & RH"), Qt::CaseInsensitive) == 0
            || c.name.compare(QLatin1String("Qingping Temp & RH M"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
