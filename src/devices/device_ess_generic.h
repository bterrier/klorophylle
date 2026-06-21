// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device.h"

#include <QtCore/QByteArray>

#include <cstdint>

namespace klr {

// Generic BLE Environmental Sensing Service (ESS, service 0x181A) sensor: any
// device advertising 0x181A that exposes the standard ESS characteristics. We
// read the common ones; absent characteristics are simply skipped by the
// GattSession. Decoding follows the ESS spec (little-endian) — note WatchFlower's
// device_ess_generic.cpp parsed these as ASCII, which is incorrect for binary
// ESS values. See docs/ess-ble-api.md.
class DeviceEssGeneric final : public Device {
public:
    QString model() const override { return QStringLiteral("Environmental sensor"); }
    DeviceType type() const override { return DeviceType::AirQuality; }

    std::vector<Reading> parseAdvertisement(const AdvertisementData &, const QDateTime &) const override
    {
        return {}; // values come over GATT
    }

    // ESS Temperature (0x2A6E): sint16, 0.01 °C.
    static std::vector<Reading> decodeTemperature(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() >= 2) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            out.push_back({ Quantity::AirTemperature, static_cast<int16_t>(d[0] + (d[1] << 8)) / 100.0,
                            Unit::DegreeCelsius, at, Provenance::Probe });
        }
        return out;
    }
    // ESS Humidity (0x2A6F): uint16, 0.01 %.
    static std::vector<Reading> decodeHumidity(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() >= 2) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            out.push_back({ Quantity::AirHumidity, static_cast<uint16_t>(d[0] + (d[1] << 8)) / 100.0,
                            Unit::Percent, at, Provenance::Probe });
        }
        return out;
    }
    // ESS Pressure (0x2A6D): uint32, 0.1 Pa -> hPa.
    static std::vector<Reading> decodePressure(const QByteArray &v, const QDateTime &at)
    {
        std::vector<Reading> out;
        if (v.size() >= 4) {
            const auto *d = reinterpret_cast<const quint8 *>(v.constData());
            const uint32_t raw = static_cast<uint32_t>(d[0]) + (static_cast<uint32_t>(d[1]) << 8)
                + (static_cast<uint32_t>(d[2]) << 16) + (static_cast<uint32_t>(d[3]) << 24);
            out.push_back({ Quantity::Pressure, raw / 1000.0, Unit::Hectopascal, at, Provenance::Probe });
        }
        return out;
    }

    std::optional<GattReadProfile> gattProfile() const override
    {
        GattReadProfile p;
        p.service = QStringLiteral("0000181a-0000-1000-8000-00805f9b34fb");
        p.reads = {
            { QStringLiteral("00002a6e-0000-1000-8000-00805f9b34fb"), GattCharacteristicRead::Access::Read, &DeviceEssGeneric::decodeTemperature, {} },
            { QStringLiteral("00002a6f-0000-1000-8000-00805f9b34fb"), GattCharacteristicRead::Access::Read, &DeviceEssGeneric::decodeHumidity, {} },
            { QStringLiteral("00002a6d-0000-1000-8000-00805f9b34fb"), GattCharacteristicRead::Access::Read, &DeviceEssGeneric::decodePressure, {} },
        };
        return p;
    }

    static bool matches(const AdvertisementContext &c)
    {
        return c.adv.serviceUuids16.contains(quint16(0x181A));
    }
};

} // namespace klr
