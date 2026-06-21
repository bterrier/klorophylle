// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping CGD1 ("Qingping Alarm Clock") thermo-hygrometer, broadcasting on the
// Qingping service 0xFDCD. See ../../docs/cgd1-ble-api.md.
class DeviceQingpingCGD1 final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping Alarm Clock"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("Qingping Alarm Clock"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
