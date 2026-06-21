// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cstdint>

namespace klr {

// Xiaomi "clock" hygrometers: LYWSD02 and MHO-C303. Connection-only here:
// subscribe to the 3-byte Temp&Humi characteristic and read the battery
// characteristic. Same custom service as the square models. See
// docs/lywsd02-ble-api.md.
class DeviceHygrotempClock final : public Device {
public:
    QString model() const override { return QStringLiteral("LYWSD02"); }
    DeviceType type() const override { return DeviceType::Thermometer; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // connection-only
    }

    // 3-byte frame: temp (int16 LE / 100), humidity (uint8).
    static std::vector<Reading> decodeRealtime(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() == 3) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            out.push_back({ Quantity::AirTemperature, static_cast<int16_t>(d[0] + (d[1] << 8)) / 100.0,
                            Unit::DegreeCelsius, at, Provenance::Probe });
            out.push_back({ Quantity::AirHumidity, double(d[2]), Unit::Percent, at, Provenance::Probe });
        }
        return out;
    }

    static std::vector<Reading> decodeBattery(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() >= 1)
            out.push_back({ Quantity::Battery, double(static_cast<quint8>(v[0])), Unit::Percent, at, Provenance::Probe });
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
        p.reads = {
            { QStringLiteral("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6"),
              GattCharacteristicRead::Access::Notify, &DeviceHygrotempClock::decodeRealtime, {} },
            { QStringLiteral("ebe0ccc4-7a0a-4b0c-8a1a-6ff2997da3a6"),
              GattCharacteristicRead::Access::Read, &DeviceHygrotempClock::decodeBattery, {} },
        };
        return p;
    }

    // --- Write commands (ADR 0026) -------------------------------------------------------
    // Clock-sync ported from WatchFlower's src/devices/device_hygrotemp_clock.cpp (the "Time"
    // characteristic EBE0CCB7). Payload: uint32 LE seconds-since-epoch + an int8 UTC-offset in
    // hours. Built from the injected `now` (no wall-clock read in the builder). The caller decides
    // whether to send it — WatchFlower only rewrote the clock when the device had drifted > 5 minutes.
    static QByteArray clockSyncPayload(const QDateTime &now)
    {
        const qint64 epoch = now.toSecsSinceEpoch();
        const auto offsetHours = static_cast<qint8>(now.offsetFromUtc() / 3600);
        QByteArray cmd;
        cmd.append(static_cast<char>(epoch & 0xff));
        cmd.append(static_cast<char>((epoch >> 8) & 0xff));
        cmd.append(static_cast<char>((epoch >> 16) & 0xff));
        cmd.append(static_cast<char>((epoch >> 24) & 0xff));
        cmd.append(static_cast<char>(offsetHours));
        return cmd;
    }

    QList<GattCommand> gattCommands(const QDateTime &now) const override
    {
        return {
            { DeviceAction::ClockSync,
              QStringLiteral("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6"),
              QStringLiteral("ebe0ccb7-7a0a-4b0c-8a1a-6ff2997da3a6"),
              clockSyncPayload(now), /*writeWithoutResponse*/ false, std::nullopt },
        };
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.compare(QLatin1String("LYWSD02"), Qt::CaseInsensitive) == 0
            || c.name.compare(QLatin1String("MHO-C303"), Qt::CaseInsensitive) == 0;
    }
};

} // namespace klr
