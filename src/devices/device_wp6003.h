// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>

#include <cstdint>

namespace klr {

// WP6003 air-quality sensor (advertised name "6003#…"). Connection-only: write a
// clock-sync + "request notify" to the TX characteristic, then read the data
// frame on the RX notify characteristic (big-endian). See docs/wp6003-ble-api.md.
class DeviceWP6003 final : public Device {
public:
    QString model() const override { return QStringLiteral("WP6003"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // connection-only
    }

    // RX frame: the data frame begins with 0x0a; values are big-endian. Other
    // frames (0xaa/0xad acks) carry no values and decode to nothing.
    static std::vector<Reading> decodeRealtime(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() < 18)
            return out;
        const auto *d = reinterpret_cast<const quint8 *>(v.constData());
        if (d[0] != 0x0a)
            return out;

        const int16_t temp = static_cast<int16_t>((d[6] << 8) + d[7]);
        const uint16_t voc = static_cast<uint16_t>((d[10] << 8) + d[11]);
        const uint16_t hcho = static_cast<uint16_t>((d[12] << 8) + d[13]);
        const uint16_t co2 = static_cast<uint16_t>((d[16] << 8) + d[17]);

        out.push_back({ Quantity::AirTemperature, temp / 10.0, Unit::DegreeCelsius, at, Provenance::Probe });
        out.push_back({ Quantity::Co2, double(co2), Unit::Ppm, at, Provenance::Probe });
        // 16383 == still pre-heating; skip VOC/HCHO until valid.
        if (voc < 16383)
            out.push_back({ Quantity::Voc, double(voc), Unit::MicrogramPerCubicMetre, at, Provenance::Probe });
        if (hcho < 16383)
            out.push_back({ Quantity::Hcho, double(hcho), Unit::MicrogramPerCubicMetre, at, Provenance::Probe });
        return out;
    }

    // --- Command byte builders (ADR 0026) ------------------------------------------------
    // Ported from WatchFlower's src/devices/device_wp6003.cpp, cross-checked against
    // docs/wp6003-ble-api.md. Both write to the TX characteristic (0xFFF1) without response.
    // The clock-sync payload is "aa" + the 2-digit year, month, day, hour, minute, second; built
    // from the injected `now` (no wall-clock read in the builder).
    static QByteArray calibratePayload() { return QByteArray::fromHex("ad"); }
    static QByteArray clockSyncPayload(const QDateTime &now)
    {
        QByteArray cmd = QByteArray::fromHex("aa");
        cmd.push_back(static_cast<char>(now.date().year() % 100));
        cmd.push_back(static_cast<char>(now.date().month()));
        cmd.push_back(static_cast<char>(now.date().day()));
        cmd.push_back(static_cast<char>(now.time().hour()));
        cmd.push_back(static_cast<char>(now.time().minute()));
        cmd.push_back(static_cast<char>(now.time().second()));
        return cmd;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        const QString tx = QStringLiteral("0000fff1-0000-1000-8000-00805f9b34fb");
        GattReadProfile p;
        p.service = QStringLiteral("0000fff0-0000-1000-8000-00805f9b34fb");
        p.triggers = {
            // "aa" + current datetime: set the device clock (the read path reads the clock here;
            // gattCommands() exposes the same builder as a standalone action).
            { tx, [] { return clockSyncPayload(QDateTime::currentDateTime()); }, true },
            // "ab": request a data notification.
            { tx, [] { return QByteArray::fromHex("ab"); }, true },
        };
        p.reads = {
            { QStringLiteral("0000fff4-0000-1000-8000-00805f9b34fb"),
              GattCharacteristicRead::Access::Notify, &DeviceWP6003::decodeRealtime, {} },
        };
        return p;
    }

    // Standalone write actions: calibrate, and a clock-sync that mirrors the read-path trigger.
    QList<GattCommand> gattCommands(const QDateTime &now) const override
    {
        const QString service = QStringLiteral("0000fff0-0000-1000-8000-00805f9b34fb");
        const QString tx = QStringLiteral("0000fff1-0000-1000-8000-00805f9b34fb");
        return {
            { DeviceAction::Calibrate, service, tx, calibratePayload(),
              /*writeWithoutResponse*/ true, std::nullopt },
            { DeviceAction::ClockSync, service, tx, clockSyncPayload(now),
              /*writeWithoutResponse*/ true, std::nullopt },
        };
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.name.startsWith(QLatin1String("6003#"), Qt::CaseInsensitive);
    }
};

} // namespace klr
