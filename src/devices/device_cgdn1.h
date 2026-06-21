// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "advertisementparser.h"
#include "device.h"

namespace klr {

// Qingping Air Monitor Lite (CGDN1): broadcasts on the Qingping service 0xFDCD
// carrying temperature, humidity, CO2, PM2.5 and PM10. Recognised by its
// advertised name "Qingping Air Monitor Lite". See ../../docs/cgdn1-ble-api.md.
class DeviceCGDN1 final : public Device {
public:
    QString model() const override { return QStringLiteral("Qingping Air Monitor Lite"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &adv, const QDateTime &at) const override
    {
        return toReadings(AdvertisementParser::decodeQingping(0xFDCD, adv.serviceData16.value(0xFDCD)), at);
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("Qingping Air Monitor Lite"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
