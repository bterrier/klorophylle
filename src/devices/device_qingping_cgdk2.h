// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping CGDK2 ("Qingping Temp RH Lite") thermo-hygrometer, broadcasting on the
// Qingping service 0xFDCD. See ../../docs/cgdk2-ble-api.md.
class DeviceQingpingCGDK2 final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping Temp RH Lite"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("Qingping Temp RH Lite"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
