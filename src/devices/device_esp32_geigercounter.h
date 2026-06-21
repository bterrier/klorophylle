// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

namespace klr {

// ESP32 DIY "GeigerCounter". Connection-only; subscribe to the realtime
// characteristic, whose payload is an ASCII float of the radioactivity
// (µSv/h). Ported from device_esp32_geigercounter.cpp.
class DeviceEsp32GeigerCounter final : public Device {
public:
    QString model() const override { return QStringLiteral("GeigerCounter"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // connection-only
    }

    static std::vector<Reading> decodeRealtime(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        bool ok = false;
        const float r = v.toFloat(&ok); // payload is an ASCII float
        if (ok && r >= 0.f && r <= 10000.f)
            out.push_back({ Quantity::Radioactivity, double(r), Unit::MicrosievertPerHour, at, Provenance::Probe });
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("eeee9a32-a000-4cbd-b00b-6b519bf2780f");
        p.reads = {
            { QStringLiteral("eeee9a32-a0d0-4cbd-b00b-6b519bf2780f"),
              GattCharacteristicRead::Access::Notify, &DeviceEsp32GeigerCounter::decodeRealtime, {} },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("GeigerCounter"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
